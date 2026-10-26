#include "app_comm.h"
#include "Uart1.h"
#include "Uart2.h"
#include "input_control.h"

/*==============================================================================
 内部定义
 *============================================================================*/

// 原始记录保留在接收环形区，编码时逐步释放，只复制名称和单位。
typedef struct
{
    uint8 name[PROJECT_NAME_MAX + 1u];
    uint8 unit[PROJECT_UNIT_MAX + 1u];
    uint8 ng;
    uint16 body_left;  // 尚未释放的括号内部字节
    uint16 suffix;     // 右括号、结果和行结束的剩余字节
    uint16 point_left; // 首组测点尚未输出的字节
    uint16 value_left; // 首组实测值尚未输出的字节
    uint16 point_skip; // 测点与实测值之间原始分隔符和空白
} AppRecord_t;

static AppRecord_t XDATA g_record;
static uint8 IDATA g_command[COMMAND_MAX + 1u];
uint8 app_state;
uint8 app_last_error;
static uint8 g_module_id, g_start_pos, g_command_len, g_command_discard;
static uint8 g_error_pending, g_tx_kind, g_tx_phase, g_tx_index, g_tx_escape;
static uint8 g_pending_byte, g_byte_pending, g_tx_finished;
static uint16 g_line_scan;
static uint8 g_x5_active;
static const char CODE *g_reply;

#define TX_NONE    0u
#define TX_JSON    1u
#define TX_LITERAL 2u

static const char CODE g_ok_text[]  = "OK";
static const char CODE g_ng_text[]  = "NG";
static const char CODE g_err_text[] = "{\"Result\":\"ERR\"}\r\n";

/**
 * @brief  判断字段两端可忽略的空白
 * @param  c 待检查字节
 * @retval 1：空格或制表符；0：其他
 */
static uint8 AppComm_IsSpace(uint8 c)
{
    return (uint8)(c == ' ' || c == '\t');
}
/**
 * @brief  将 ASCII 小写字母转换为大写
 * @param  c 待转换字节
 * @retval 转换后的字节
 */
static uint8 AppComm_ToUpper(uint8 c)
{
    if (c >= 'a' && c <= 'z')
    {
        return c - ('a' - 'A');
    }
    return c;
}

/**
 * @brief  去除接收区指定范围两端的空白
 * @param  begin/end 范围起止地址，结束位置不含在内
 * @retval 无
 */
static void AppComm_TrimRange(uint16 *begin, uint16 *end)
{
    while (*begin < *end && AppComm_IsSpace(Uart1_Peek(*begin)))
    {
        ++*begin;
    }
    while (*end > *begin && AppComm_IsSpace(Uart1_Peek(*end - 1u)))
    {
        --*end;
    }
}

/**
 * @brief  比较字符串与程序存储区常量
 * @param  s 待比较字符串
 * @param  expected 期望字符串
 * @retval 1：相同；0：不同
 */
static uint8 AppComm_StringEquals(const uint8 *s, const char CODE *expected)
{
    while (*s && *s == (uint8)*expected)
    {
        ++s;
        ++expected;
    }
    return (uint8)(*s == 0u && *expected == '\0');
}

/**
 * @brief  校验十进制或科学计数法，不修改原始精度
 * @param  begin/end 接收区起止偏移
 * @retval 1：数值有效；0：格式错误
 */
static uint8 AppComm_IsNumber(uint16 begin, uint16 end)
{
    uint8 c, digits;
    if (begin == end)
    {
        return 0u;
    }
    c = Uart1_Peek(begin);
    if (c == '+' || c == '-')
    {
        ++begin;
    }
    digits = 0u;
    while (begin < end)
    {
        c = Uart1_Peek(begin);
        if (c < '0' || c > '9')
        {
            break;
        }
        digits = 1u;
        ++begin;
    }
    if (begin < end && Uart1_Peek(begin) == '.')
    {
        ++begin;
        while (begin < end)
        {
            c = Uart1_Peek(begin);
            if (c < '0' || c > '9')
            {
                break;
            }
            digits = 1u;
            ++begin;
        }
    }
    if (!digits)
    {
        return 0u;
    }
    if (begin < end && AppComm_ToUpper(Uart1_Peek(begin)) == 'E')
    {
        ++begin;
        if (begin < end && (Uart1_Peek(begin) == '+' || Uart1_Peek(begin) == '-'))
        {
            ++begin;
        }
        digits = 0u;
        while (begin < end)
        {
            c = Uart1_Peek(begin);
            if (c < '0' || c > '9')
            {
                break;
            }
            digits = 1u;
            ++begin;
        }
        if (!digits)
        {
            return 0u;
        }
    }
    return (uint8)(begin == end);
}

/**
 * @brief  校验可输出到 JSON 的 UTF-8 文本
 * @param  begin/end 接收区起止偏移
 * @retval 1：编码有效；0：控制字符或编码错误
 */
static uint8 AppComm_IsValidText(uint16 begin, uint16 end)
{
    uint8 c, remaining, low, high;
    remaining = 0u;
    low       = 0x80u;
    high      = 0xBFu;
    while (begin < end)
    {
        c = Uart1_Peek(begin++);
        if (c < 0x20u || c == 0x7Fu)
        {
            return 0u;
        }
        // 拒绝截断、过长编码、代理码点和超出 Unicode 范围的字符。
        if (remaining)
        {
            if (c < low || c > high)
            {
                return 0u;
            }
            --remaining;
            low  = 0x80u;
            high = 0xBFu;
        }
        else if (c >= 0x80u)
        {
            if (c >= 0xC2u && c <= 0xDFu)
            {
                remaining = 1u;
            }
            else if (c >= 0xE0u && c <= 0xEFu)
            {
                remaining = 2u;
                if (c == 0xE0u)
                {
                    low = 0xA0u;
                }
                if (c == 0xEDu)
                {
                    high = 0x9Fu;
                }
            }
            else if (c >= 0xF0u && c <= 0xF4u)
            {
                remaining = 3u;
                if (c == 0xF0u)
                {
                    low = 0x90u;
                }
                if (c == 0xF4u)
                {
                    high = 0x8Fu;
                }
            }
            else
            {
                return 0u;
            }
        }
    }
    return (uint8)(remaining == 0u);
}

/**
 * @brief  校验并复制短字段，预留字符串结束符
 * @param  begin/end 接收区起止偏移
 * @param  dest 目标缓冲
 * @param  maximum 不含结束符的容量
 * @retval 1：成功；0：超限或编码错误
 */
static uint8 AppComm_CopyField(uint16 begin, uint16 end, uint8 *dest, uint8 maximum)
{
    uint8 at;
    if (end - begin > maximum || !AppComm_IsValidText(begin, end))
    {
        return 0u;
    }
    at = 0u;
    while (begin < end)
    {
        dest[at++] = Uart1_Peek(begin++);
    }
    dest[at] = 0u;
    return 1u;
}

/**
 * @brief  校验所有测量组的结构、测点编码和数值
 * @param  begin/end 括号内部的接收区范围
 * @retval 1：有效；0：存在错误组
 */
static uint8 AppComm_ValidateBody(uint16 begin, uint16 end)
{
    uint16 stop, eq, a, b;
    uint8 c;
    AppComm_TrimRange(&begin, &end);
    if (begin == end)
    {
        return 1u;
    }
    while (begin < end)
    {
        stop = begin;
        while (stop < end && Uart1_Peek(stop) != ',')
        {
            ++stop;
        }
        a = begin;
        b = stop;
        AppComm_TrimRange(&a, &b);
        if (a == b)
        {
            return 0u; // 拒绝开头逗号及连续空组
        }
        eq = a;
        while (eq < b && Uart1_Peek(eq) != '=')
        {
            c = Uart1_Peek(eq);
            if (c < 0x20u || c == '(' || c == ')')
            {
                return 0u;
            }
            ++eq;
        }
        if (eq == b)
        {
            return 0u;
        }
        begin = a;
        a     = eq;
        AppComm_TrimRange(&begin, &a);
        if (begin == a || !AppComm_IsValidText(begin, a))
        {
            return 0u;
        }
        a = eq + 1u;
        AppComm_TrimRange(&a, &b);
        if (!AppComm_IsNumber(a, b))
        {
            return 0u;
        }
        begin = stop;
        if (begin < end)
        {
            ++begin;
        }
        while (begin < end && AppComm_IsSpace(Uart1_Peek(begin)))
        {
            ++begin;
        }
    }
    return 1u;
}

/**
 * @brief  校验完整主板记录并保存流式编码所需信息
 * @param  length 不含 CR 或 LF 的记录长度
 * @retval 1：有效；0：格式或容量错误
 */
static uint8 AppComm_ParseRecord(uint16 length)
{
    uint16 pos, begin, end, body_begin, body_end;
    uint8 field;
    pos = 0u;
    for (field = 0u; field < 4u; ++field)
    {
        begin = pos;
        while (pos < length && Uart1_Peek(pos) != ',')
        {
            ++pos;
        }
        if (pos == length)
        {
            return 0u;
        }
        end = pos++;
        AppComm_TrimRange(&begin, &end);
        if (field == 0u)
        {
            if (begin == end || !AppComm_CopyField(begin, end, g_record.name, PROJECT_NAME_MAX))
            {
                return 0u;
            }
            if (AppComm_StringEquals(g_record.name, "MH") || AppComm_StringEquals(g_record.name, "Result"))
            {
                return 0u;
            }
        }
        else if (field == 3u)
        {
            if (!AppComm_CopyField(begin, end, g_record.unit, PROJECT_UNIT_MAX))
            {
                return 0u;
            }
        }
        else if (!AppComm_IsNumber(begin, end))
        {
            return 0u;
        }
    }
    while (pos < length && AppComm_IsSpace(Uart1_Peek(pos)))
    {
        ++pos;
    }
    if (pos == length || Uart1_Peek(pos++) != '(')
    {
        return 0u;
    }
    body_begin = pos;
    while (pos < length && Uart1_Peek(pos) != ')')
    {
        ++pos;
    }
    if (pos == length)
    {
        return 0u;
    }
    body_end = pos++;
    while (pos < length && AppComm_IsSpace(Uart1_Peek(pos)))
    {
        ++pos;
    }
    if (pos == length || Uart1_Peek(pos++) != ',')
    {
        return 0u;
    }
    begin = pos;
    end   = length;
    AppComm_TrimRange(&begin, &end);
    if (end - begin != 2u)
    {
        return 0u;
    }
    if (AppComm_ToUpper(Uart1_Peek(begin)) == 'O' && AppComm_ToUpper(Uart1_Peek(begin + 1u)) == 'K')
    {
        g_record.ng = 0u;
    }
    else if (AppComm_ToUpper(Uart1_Peek(begin)) == 'N' && AppComm_ToUpper(Uart1_Peek(begin + 1u)) == 'G')
    {
        g_record.ng = 1u;
    }
    else
    {
        return 0u;
    }
    if (!AppComm_ValidateBody(body_begin, body_end))
    {
        return 0u;
    }
    g_record.body_left = body_end - body_begin;
    g_record.suffix    = length - body_end + 1u; // 含一个已收到的 CR 或 LF
    Uart1_Drop(body_begin);
    return 1u;
}

/**
 * @brief  定位首组测点和数值，不分配完整记录副本
 * @param  无
 * @retval 1：存在首组；0：括号为空
 */
static uint8 AppComm_LoadFirstMeasurement(void)
{
    uint16 stop, eq, begin, end, point_begin, point_end;
    while (g_record.body_left && AppComm_IsSpace(Uart1_Peek(0u)))
    {
        Uart1_Drop(1u);
        --g_record.body_left;
    }
    if (!g_record.body_left)
    {
        return 0u;
    }
    stop = 0u;
    while (stop < g_record.body_left && Uart1_Peek(stop) != ',')
    {
        ++stop;
    }
    eq = 0u;
    while (eq < stop && Uart1_Peek(eq) != '=')
    {
        ++eq;
    }
    begin = eq + 1u;
    end   = stop;
    AppComm_TrimRange(&begin, &end);
    point_begin = 0u;
    point_end   = eq;
    AppComm_TrimRange(&point_begin, &point_end);
    g_record.point_left = point_end - point_begin;
    g_record.point_skip = begin - point_end;
    g_record.value_left = end - begin;
    Uart1_Drop(point_begin);
    g_record.body_left -= point_begin;
    return 1u;
}

/**
 * @brief  逐字节转义测点并释放已消费的接收数据
 * @param  out 输出字节地址
 * @retval 1：取得字节；0：测点已结束
 */
static uint8 AppComm_NextPointByte(uint8 *out)
{
    uint8 c;
    if (!g_record.point_left)
    {
        return 0u;
    }
    c = Uart1_Peek(0u);
    if (!g_tx_escape && (c == '"' || c == '\\'))
    {
        g_tx_escape = 1u;
        *out        = '\\';
    }
    else
    {
        g_tx_escape = 0u;
        *out        = c;
        Uart1_Drop(1u);
        --g_record.point_left;
        --g_record.body_left;
    }
    return 1u;
}

/**
 * @brief  逐字节转义短字段中的引号和反斜杠
 * @param  str 原始字符串
 * @param  out 输出字节地址
 * @retval 1：取得字节；0：字段已结束
 */
static uint8 AppComm_NextStringByte(const uint8 *str, uint8 *out)
{
    uint8 c;
    c = str[g_tx_index];
    if (!c)
    {
        return 0u;
    }
    if (g_tx_escape)
    {
        g_tx_escape = 0u;
        ++g_tx_index;
        *out = c;
    }
    else if (c == '"' || c == '\\')
    {
        g_tx_escape = 1u;
        *out        = '\\';
    }
    else
    {
        ++g_tx_index;
        *out = c;
    }
    return 1u;
}

/**
 * @brief  按固定阶段流式编码一条完整 JSON
 * @param  out 输出字节地址
 * @retval 1：取得字节；0：整帧结束
 */
static uint8 AppComm_NextJsonByte(uint8 *out)
{
    const char CODE *text;
    while (1)
    {
        text = "";
        switch (g_tx_phase)
        {
            case 0:
                text = "{\"MH\":\"";
                break;
            case 1:
                *out = '0' + g_module_id;
                ++g_tx_phase;
                return 1u;
            case 2:
                text = "\",\"Result\":\"";
                break;
            case 3:
                text = g_record.ng ? g_ng_text : g_ok_text;
                break;
            case 4:
                text = "\",\"";
                break;
            case 5:
                if (AppComm_NextStringByte(g_record.name, out))
                {
                    return 1u;
                }
                g_tx_index = 0u;
                ++g_tx_phase;
                continue;
            case 6:
                text = "\":\"";
                break;
            case 7: // 只定位首组，空括号直接输出本行结果
                g_tx_phase = AppComm_LoadFirstMeasurement() ? 8u : 14u;
                continue;
            case 8: // 测点可能很长，直接从接收区逐字节转义
                if (AppComm_NextPointByte(out))
                {
                    return 1u;
                }
                ++g_tx_phase;
                continue;
            case 9:
                Uart1_Drop(g_record.point_skip);
                g_record.body_left -= g_record.point_skip;
                *out = '=';
                ++g_tx_phase;
                return 1u;
            case 10: // 保留首组数值精度，输出完后释放其余测量组
                if (g_record.value_left)
                {
                    *out = Uart1_Peek(0u);
                    Uart1_Drop(1u);
                    --g_record.value_left;
                    --g_record.body_left;
                    return 1u;
                }
                Uart1_Drop(g_record.body_left);
                g_record.body_left = 0u;
                ++g_tx_phase;
                continue;
            case 11:
                ++g_tx_phase;
                if (g_record.unit[0])
                {
                    *out = ' ';
                    return 1u;
                }
                continue;
            case 12:
                if (AppComm_NextStringByte(g_record.unit, out))
                {
                    return 1u;
                }
                g_tx_index = 0u;
                ++g_tx_phase;
                continue;
            case 13:
                text = ":";
                break;
            case 14:
                text = g_record.ng ? g_ng_text : g_ok_text;
                break;
            case 15: // 释放右括号、结果和一个行结束符
                Uart1_Drop(g_record.suffix);
                g_record.suffix = 0u;
                ++g_tx_phase;
                continue;
            case 16:
                text = "\"}\r\n";
                break;
            default:
                return 0u;
        }
        if (text[g_tx_index])
        {
            *out = (uint8)text[g_tx_index++];
            return 1u;
        }
        g_tx_index = 0u;
        ++g_tx_phase;
    }
}

/**
 * @brief  初始化一帧上位机输出的发送状态
 * @param  kind JSON 或固定回复类型
 * @retval 无
 */
static void AppComm_StartTx(uint8 kind)
{
    g_tx_kind      = kind;
    g_tx_phase     = 0u;
    g_tx_index     = 0u;
    g_tx_escape    = 0u;
    g_byte_pending = 0u;
    g_tx_finished  = 0u;
}

/**
 * @brief  在 UART2 空闲时发送下一个字节
 * @param  无
 * @retval 无
 */
static void AppComm_PollTx(void)
{
    if (!Uart2_TxIdle())
    {
        return;
    }
    if (g_tx_finished)
    {
        g_tx_kind = TX_NONE;
        return;
    }
    if (!g_byte_pending)
    {
        if (g_tx_kind == TX_JSON)
        {
            g_byte_pending = AppComm_NextJsonByte(&g_pending_byte);
        }
        else
        {
            g_pending_byte = (uint8)g_reply[g_tx_index];
            g_byte_pending = (uint8)(g_pending_byte != 0u);
            if (g_byte_pending)
            {
                ++g_tx_index;
            }
        }
        if (!g_byte_pending)
        {
            g_tx_finished = 1u;
            return;
        }
    }
    if (Uart2_TryWrite(g_pending_byte))
    {
        g_byte_pending = 0u;
    }
}

/**
 * @brief  记录首个异常，待当前合法 JSON 发完后回复 ERR
 * @param  reason 调试错误码
 * @retval 无
 */
static void AppComm_SetError(uint8 reason)
{
    if (!g_error_pending)
    {
        app_last_error = reason;
    }
    g_error_pending = 1u;
    app_state       = APP_IDLE;
    // 保留已经开始输出的合法记录，完整发送后再回复 ERR。
}

/**
 * @brief  锁定已核对的模号并准备下发 test
 * @param  id 模号，范围为 1～4
 * @retval 无
 */
static void AppComm_StartSession(uint8 id)
{
    g_module_id = id;
    Uart1_ClearRx();
    g_line_scan    = 0u;
    g_start_pos    = 0u;
    app_last_error = APP_ERROR_NONE;
    app_state      = APP_SENDING_START;
}

/**
 * @brief  按 828C 协议用 Result JSON 回复模号，不改变测试会话
 * @param  id 唯一有效模号，0 表示输入无效
 * @retval 无
 */
static void AppComm_ReplyModule(uint8 id)
{
    switch (id)
    {
        case 1u:
            g_reply = "{\"Result\":\"1\"}\r\n";
            break;
        case 2u:
            g_reply = "{\"Result\":\"2\"}\r\n";
            break;
        case 3u:
            g_reply = "{\"Result\":\"3\"}\r\n";
            break;
        case 4u:
            g_reply = "{\"Result\":\"4\"}\r\n";
            break;
        default:
            g_reply = g_err_text; // 查询失败只回复 ERR，不终止已有会话
            break;
    }
    AppComm_StartTx(TX_LITERAL);
}

/**
 * @brief  执行已经收完整的上位机命令
 * @param  无
 * @retval 无
 */
static void AppComm_ExecuteCommand(void)
{
    uint8 begin, end, i, module;
    begin = 0u;
    end   = g_command_len;
    while (begin < end && AppComm_IsSpace(g_command[begin]))
    {
        ++begin;
    }
    while (end > begin && AppComm_IsSpace(g_command[end - 1u]))
    {
        --end;
    }
    for (i = 0u; begin < end; ++i)
    {
        g_command[i] = g_command[begin++];
    }
    g_command[i]  = 0u;
    g_command_len = 0u;
    if (!i)
    {
        return;
    }
    if (i == 2u && AppComm_ToUpper(g_command[0]) == 'O' && AppComm_ToUpper(g_command[1]) == 'K')
    {
        return;
    }
    if (AppComm_StringEquals(g_command, "(RD_NUM)"))
    {
        module = InputControl_GetModule(InputControl_ReadActive());
        AppComm_ReplyModule(module);
    }
    else if (i == 9u && g_command[0] == '(' && g_command[1] == 'S' &&
             g_command[2] == 'T' && g_command[3] == 'A' && g_command[4] == 'R' &&
             g_command[5] == 'T' && g_command[6] == ',' && g_command[8] == ')' &&
             g_command[7] >= '1' && g_command[7] <= '4')
    {
        module = InputControl_GetModule(InputControl_ReadActive());
        if (module && module == g_command[7] - '0')
        {
            AppComm_StartSession(module);
        }
        else
        {
            AppComm_SetError(APP_ERROR_MODULE);
        }
    }
    else
    {
        AppComm_SetError(APP_ERROR_COMMAND);
    }
}

/**
 * @brief  接收命令字节，处理帧结束和异常重同步
 * @param  c 新接收的字节
 * @retval 无
 */
static void AppComm_HandleCommandByte(uint8 c)
{
    if (g_command_discard)
    {
        if (c == '(')
        {
            g_command_discard = 0u;
            g_command_len     = 0u;
        }
        else
        {
            if (c == ')' || c == '\r' || c == '\n')
            {
                g_command_discard = 0u;
            }
            return;
        }
    }
    if (c == '\r' || c == '\n')
    {
        AppComm_ExecuteCommand();
        return;
    }
    if (c < 0x20u && c != '\t')
    {
        g_command_len     = 0u;
        g_command_discard = 1u;
        AppComm_SetError(APP_ERROR_COMMAND);
        return;
    }
    if (g_command_len == COMMAND_MAX)
    {
        g_command_len     = 0u;
        g_command_discard = 1u;
        AppComm_SetError(APP_ERROR_COMMAND);
        return;
    }
    g_command[g_command_len++] = c;
    // 支持不带 CRLF 的两个字节 OK，避免污染下一条命令。
    if (g_command_len == 2u && AppComm_ToUpper(g_command[0]) == 'O' && AppComm_ToUpper(g_command[1]) == 'K')
    {
        g_command_len = 0u;
        return;
    }
    if (c == ')')
    {
        AppComm_ExecuteCommand();
    }
}

/**
 * @brief  忽略两端空白和大小写比较主板标记
 * @param  length 标记行长度
 * @param  s 期望标记
 * @retval 1：匹配；0：不匹配
 */
static uint8 AppComm_IsMarker(uint16 length, const char CODE *s)
{
    uint16 begin, end;
    begin = 0u;
    end   = length;
    AppComm_TrimRange(&begin, &end);
    while (begin < end && *s)
    {
        if (AppComm_ToUpper(Uart1_Peek(begin++)) != AppComm_ToUpper((uint8)*s++))
        {
            return 0u;
        }
    }
    return (uint8)(begin == end && *s == '\0');
}

/**
 * @brief  扫描主板行边界并启动记录转换
 * @param  无
 * @retval 无
 */
static void AppComm_PollBoard(void)
{
    uint16 available;
    uint8 c, budget;
    available = Uart1_Count();
    budget    = 32u;
    while (g_line_scan < available && budget--)
    {
        c = Uart1_Peek(g_line_scan);
        if (c == '\r' || c == '\n')
        {
            if (AppComm_IsMarker(g_line_scan, "") || AppComm_IsMarker(g_line_scan, "test"))
            {
                Uart1_Drop(g_line_scan + 1u);
            }
            else if (AppComm_IsMarker(g_line_scan, "end"))
            {
                if (app_state == APP_WAIT_FIRST)
                {
                    AppComm_SetError(APP_ERROR_FORMAT);
                    return;
                }
                Uart1_ClearRx();
                app_state = APP_IDLE;
            }
            else if (AppComm_ParseRecord(g_line_scan))
            {
                app_state = APP_RECEIVING;
                AppComm_StartTx(TX_JSON);
            }
            else
            {
                AppComm_SetError(APP_ERROR_FORMAT);
                return;
            }
            g_line_scan = 0u;
            return;
        }
        if (++g_line_scan > INPUT_LINE_MAX)
        {
            AppComm_SetError(APP_ERROR_LINE_LONG);
            return;
        }
    }
}

/**
 * @brief  非阻塞下发 test 指令，完成后持续等待主板记录
 * @param  无
 * @retval 无
 */
static void AppComm_PollStartCommand(void)
{
    if (g_start_pos < 6u)
    {
        if (Uart1_TryWrite((uint8) "test\r\n"[g_start_pos]))
        {
            ++g_start_pos;
        }
    }
    else if (Uart1_TxIdle())
    {
        app_state = APP_WAIT_FIRST; // 等待首条记录，不设置超时
    }
}

/*==============================================================================
 对外函数
 *============================================================================*/

/**
 * @brief  初始化协议会话、发送状态与接收缓冲
 * @param  无
 * @retval 无
 */
void AppComm_Init(void)
{
    app_state      = APP_IDLE;
    app_last_error = APP_ERROR_NONE;
    g_module_id    = 0u; // 仅由有效 START 或 X5 触发锁定模号
    g_start_pos = g_command_len = g_command_discard = 0u;
    g_error_pending = g_tx_kind = g_tx_phase = g_tx_index = g_tx_escape = 0u;
    g_byte_pending = g_tx_finished = 0u;
    g_line_scan                    = 0u;
    g_x5_active                    = 0u; // 上电已为低的 X5 在首次轮询时也检测一次
    Uart1_ClearRx();
    Uart2_ClearRx();
}

/**
 * @brief  非阻塞轮询命令、启动条件和主板记录
 * @param  无
 * @retval 无
 */
void AppComm_Poll(void)
{
    uint8 c, inputs, x5_trigger, module;
    inputs      = InputControl_ReadActive();
    x5_trigger  = (uint8)((inputs & 0x10u) && !g_x5_active);
    g_x5_active = (uint8)(inputs & 0x10u); // 忙时同样更新锁存，不排队补发
    if (Uart1_Overflow() && app_state != APP_IDLE)
    {
        AppComm_SetError(APP_ERROR_UART1_FULL);
    }
    if (Uart2_Overflow())
    {
        Uart2_ClearRx();
        g_command_len     = 0u;
        g_command_discard = 1u;
        AppComm_SetError(APP_ERROR_UART2_FULL);
    }
    if (g_tx_kind)
    {
        AppComm_PollTx();
        return;
    }
    if (g_error_pending)
    {
        Uart1_ClearRx();
        g_line_scan     = 0u;
        g_error_pending = 0u;
        g_reply         = g_err_text;
        AppComm_StartTx(TX_LITERAL);
        return;
    }
    if (app_state == APP_SENDING_START)
    {
        AppComm_PollStartCommand();
        return;
    }
    if (Uart2_ReadByte(&c))
    {
        AppComm_HandleCommandByte(c);
        if (g_tx_kind || g_error_pending || app_state == APP_SENDING_START)
        {
            return;
        }
    }
    if (app_state == APP_IDLE)
    {
        Uart1_ClearRx();
        g_line_scan = 0u;
        if (x5_trigger && !g_command_len && !g_command_discard)
        {
            module = InputControl_GetModule(inputs);
            if (module)
            {
                AppComm_StartSession(module);
            }
            else
            {
                AppComm_SetError(APP_ERROR_MODULE);
            }
        }
        return;
    }
    AppComm_PollBoard();
}
