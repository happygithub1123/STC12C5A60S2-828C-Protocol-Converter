#include "board_config.h"
#include "app_comm.h"
#include "System.h"
#include "Uart1.h"
#include "Uart2.h"

/* No complete-line or JSON array: validated UART1 bytes remain in its ring
 * until the streaming encoder consumes them. Only short metadata is copied. */
typedef struct {
    uint8 name[PROJECT_NAME_MAX + 1u];
    uint8 unit[PROJECT_UNIT_MAX + 1u];
    uint8 ng;
    uint16 body_left;
    uint16 suffix;
    uint16 value_left;
    uint16 value_skip;
} Record;

static Record XDATA record;
static uint8 IDATA command[COMMAND_MAX + 1u];
uint8 app_state;
uint8 app_last_error;
static uint8 module_id, start_pos, command_len, command_discard;
static uint8 error_pending, tx_kind, tx_phase, tx_index, tx_escape;
static uint8 pending_byte, byte_pending, tx_finished;
static uint16 line_scan, first_started;
static const char CODE *reply;

#define TX_NONE    0u
#define TX_JSON    1u
#define TX_LITERAL 2u

static const char CODE ok_text[] = "OK";
static const char CODE ng_text[] = "NG";
static const char CODE err_text[] = "{\"Result\":\"ERR\"}\r\n";

static uint8 Space(uint8 c) { return (uint8)(c == ' ' || c == '\t'); }
static uint8 Upper(uint8 c)
{
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

static void Trim(uint16 *begin, uint16 *end)
{
    while (*begin < *end && Space(Uart1_Peek(*begin))) ++*begin;
    while (*end > *begin && Space(Uart1_Peek(*end - 1u))) --*end;
}

static uint8 Equal(const uint8 *s, const char CODE *expected)
{
    while (*s && *s == (uint8)*expected) { ++s; ++expected; }
    return (uint8)(*s == 0u && *expected == '\0');
}

/* Validate decimal syntax without changing precision or re-judging limits. */
static uint8 Number(uint16 begin, uint16 end)
{
    uint8 c, digits;
    if (begin == end) return 0u;
    c = Uart1_Peek(begin);
    if (c == '+' || c == '-') ++begin;
    digits = 0u;
    while (begin < end) {
        c = Uart1_Peek(begin);
        if (c < '0' || c > '9') break;
        digits = 1u;
        ++begin;
    }
    if (begin < end && Uart1_Peek(begin) == '.') {
        ++begin;
        while (begin < end) {
            c = Uart1_Peek(begin);
            if (c < '0' || c > '9') break;
            digits = 1u;
            ++begin;
        }
    }
    if (!digits) return 0u;
    if (begin < end && Upper(Uart1_Peek(begin)) == 'E') {
        ++begin;
        if (begin < end && (Uart1_Peek(begin) == '+' || Uart1_Peek(begin) == '-')) ++begin;
        digits = 0u;
        while (begin < end) {
            c = Uart1_Peek(begin);
            if (c < '0' || c > '9') break;
            digits = 1u;
            ++begin;
        }
        if (!digits) return 0u;
    }
    return (uint8)(begin == end);
}

static uint8 CopyField(uint16 begin, uint16 end, uint8 *dest, uint8 maximum)
{
    uint8 c, at, remaining, low, high;
    if (end - begin > maximum) return 0u;
    at = 0u;
    remaining = 0u;
    low = 0x80u;
    high = 0xBFu;
    while (begin < end) {
        c = Uart1_Peek(begin++);
        if (c < 0x20u || c == 0x7Fu) return 0u;
        /* JSON text is UTF-8. Reject truncated/overlong sequences, surrogates
         * and values beyond U+10FFFF before emitting any part of the frame. */
        if (remaining) {
            if (c < low || c > high) return 0u;
            --remaining;
            low = 0x80u;
            high = 0xBFu;
        } else if (c >= 0x80u) {
            if (c >= 0xC2u && c <= 0xDFu) remaining = 1u;
            else if (c >= 0xE0u && c <= 0xEFu) {
                remaining = 2u;
                if (c == 0xE0u) low = 0xA0u;
                if (c == 0xEDu) high = 0x9Fu;
            } else if (c >= 0xF0u && c <= 0xF4u) {
                remaining = 3u;
                if (c == 0xF0u) low = 0x90u;
                if (c == 0xF4u) high = 0x8Fu;
            } else return 0u;
        }
        dest[at++] = c;
    }
    dest[at] = 0u;
    return (uint8)(remaining == 0u);
}

static uint8 ValidateBody(uint16 begin, uint16 end)
{
    uint16 stop, eq, a, b;
    uint8 c;
    Trim(&begin, &end);
    if (begin == end) return 1u;
    while (begin < end) {
        stop = begin;
        while (stop < end && Uart1_Peek(stop) != ',') ++stop;
        a = begin;
        b = stop;
        Trim(&a, &b);
        if (a == b) return 0u; /* Reject leading or consecutive commas. */
        eq = a;
        while (eq < b && Uart1_Peek(eq) != '=') {
            c = Uart1_Peek(eq);
            if (c < 0x20u || c == '(' || c == ')') return 0u;
            ++eq;
        }
        if (eq == b) return 0u;
        begin = a;
        a = eq;
        Trim(&begin, &a);
        if (begin == a) return 0u;
        a = eq + 1u;
        Trim(&a, &b);
        if (!Number(a, b)) return 0u;
        begin = stop;
        if (begin < end) ++begin;
        while (begin < end && Space(Uart1_Peek(begin))) ++begin;
    }
    return 1u;
}

/* On success only: consume the prefix; retain body + suffix for encoder. */
static uint8 ParseRecord(uint16 length)
{
    uint16 pos, begin, end, body_begin, body_end;
    uint8 field;
    pos = 0u;
    for (field = 0u; field < 4u; ++field) {
        begin = pos;
        while (pos < length && Uart1_Peek(pos) != ',') ++pos;
        if (pos == length) return 0u;
        end = pos++;
        Trim(&begin, &end);
        if (field == 0u) {
            if (begin == end || !CopyField(begin, end, record.name, PROJECT_NAME_MAX)) return 0u;
            if (Equal(record.name, "MH") || Equal(record.name, "Result")) return 0u;
        } else if (field == 3u) {
            if (!CopyField(begin, end, record.unit, PROJECT_UNIT_MAX)) return 0u;
        } else if (!Number(begin, end)) return 0u;
    }
    while (pos < length && Space(Uart1_Peek(pos))) ++pos;
    if (pos == length || Uart1_Peek(pos++) != '(') return 0u;
    body_begin = pos;
    while (pos < length && Uart1_Peek(pos) != ')') ++pos;
    if (pos == length) return 0u;
    body_end = pos++;
    while (pos < length && Space(Uart1_Peek(pos))) ++pos;
    if (pos == length || Uart1_Peek(pos++) != ',') return 0u;
    begin = pos;
    end = length;
    Trim(&begin, &end);
    if (end - begin != 2u) return 0u;
    if (Upper(Uart1_Peek(begin)) == 'O' && Upper(Uart1_Peek(begin + 1u)) == 'K') record.ng = 0u;
    else if (Upper(Uart1_Peek(begin)) == 'N' && Upper(Uart1_Peek(begin + 1u)) == 'G') record.ng = 1u;
    else return 0u;
    if (!ValidateBody(body_begin, body_end)) return 0u;
    record.body_left = body_end - body_begin;
    record.suffix = length - body_end + 1u; /* Includes one received CR or LF. */
    Uart1_Drop(body_begin);
    return 1u;
}

static uint8 LoadValue(void)
{
    uint16 stop, eq, begin, end;
    while (record.body_left && Space(Uart1_Peek(0u))) {
        Uart1_Drop(1u);
        --record.body_left;
    }
    if (!record.body_left) return 0u;
    stop = 0u;
    while (stop < record.body_left && Uart1_Peek(stop) != ',') ++stop;
    eq = 0u;
    while (eq < stop && Uart1_Peek(eq) != '=') ++eq;
    begin = eq + 1u;
    end = stop;
    Trim(&begin, &end);
    record.value_left = end - begin;
    record.value_skip = stop - end;
    if (stop < record.body_left) ++record.value_skip;
    Uart1_Drop(begin);
    record.body_left -= begin;
    return 1u;
}

static uint8 EscapedString(const uint8 *str, uint8 *out)
{
    uint8 c;
    c = str[tx_index];
    if (!c) return 0u;
    if (tx_escape) {
        tx_escape = 0u;
        ++tx_index;
        *out = c;
    } else if (c == '"' || c == '\\') {
        tx_escape = 1u;
        *out = '\\';
    } else {
        ++tx_index;
        *out = c;
    }
    return 1u;
}

static uint8 NextJsonByte(uint8 *out)
{
    const char CODE *text;
    while (1) {
        text = "";
        switch (tx_phase) {
        case 0: text = "{\"MH\":\""; break;
        case 1: *out = '0' + module_id; ++tx_phase; return 1u;
        case 2: text = "\",\"Result\":\""; break;
        case 3: text = record.ng ? ng_text : ok_text; break;
        case 4: text = "\",\""; break;
        case 5:
            if (EscapedString(record.name, out)) return 1u;
            tx_index = 0u; ++tx_phase; continue;
        case 6: text = "\":\""; break;
        case 7:
            tx_phase = LoadValue() ? 8u : 13u;
            continue;
        case 8:
            if (record.value_left) {
                *out = Uart1_Peek(0u);
                Uart1_Drop(1u);
                --record.value_left;
                --record.body_left;
                return 1u;
            }
            Uart1_Drop(record.value_skip);
            record.body_left -= record.value_skip;
            ++tx_phase;
            continue;
        case 9:
            if (EscapedString(record.unit, out)) return 1u;
            tx_index = 0u; ++tx_phase; continue;
        case 10: text = ":"; break;
        case 11: text = record.ng ? ng_text : ok_text; break;
        case 12:
            if (LoadValue()) { tx_phase = 8u; *out = ';'; return 1u; }
            tx_phase = 14u;
            continue;
        case 13: text = record.ng ? ng_text : ok_text; break;
        case 14:
            Uart1_Drop(record.suffix);
            record.suffix = 0u;
            ++tx_phase;
            continue;
        case 15: text = "\"}\r\n"; break;
        default: return 0u;
        }
        if (text[tx_index]) { *out = (uint8)text[tx_index++]; return 1u; }
        tx_index = 0u;
        ++tx_phase;
    }
}

static void StartTx(uint8 kind)
{
    tx_kind = kind;
    tx_phase = 0u;
    tx_index = 0u;
    tx_escape = 0u;
    byte_pending = 0u;
    tx_finished = 0u;
}

static void PollTx(void)
{
    if (!Uart2_TxIdle()) return;
    if (tx_finished) { tx_kind = TX_NONE; return; }
    if (!byte_pending) {
        if (tx_kind == TX_JSON) byte_pending = NextJsonByte(&pending_byte);
        else {
            pending_byte = (uint8)reply[tx_index];
            byte_pending = (uint8)(pending_byte != 0u);
            if (byte_pending) ++tx_index;
        }
        if (!byte_pending) { tx_finished = 1u; return; }
    }
    if (Uart2_TryWrite(pending_byte)) byte_pending = 0u;
}

static void Fail(uint8 reason)
{
    if (!error_pending) app_last_error = reason;
    error_pending = 1u;
    app_state = APP_IDLE;
    /* Preserve a fully validated record already being serialized. */
}

static void BeginTest(uint8 id)
{
    module_id = id;
    Uart1_ClearRx();
    line_scan = 0u;
    start_pos = 0u;
    app_last_error = APP_ERROR_NONE;
    app_state = APP_SENDING_START;
}

static void ExecuteCommand(void)
{
    uint8 begin, end, i;
    begin = 0u;
    end = command_len;
    while (begin < end && Space(command[begin])) ++begin;
    while (end > begin && Space(command[end - 1u])) --end;
    for (i = 0u; begin < end; ++i) command[i] = command[begin++];
    command[i] = 0u;
    command_len = 0u;
    if (!i) return;
    if (i == 2u && Upper(command[0]) == 'O' && Upper(command[1]) == 'K') return;
    if (Equal(command, "(RD_NUM)")) {
#if FIXTURE_DEFAULT_ID == 1
        reply = "{\"Result\":\"1\"}\r\n";
#elif FIXTURE_DEFAULT_ID == 2
        reply = "{\"Result\":\"2\"}\r\n";
#elif FIXTURE_DEFAULT_ID == 3
        reply = "{\"Result\":\"3\"}\r\n";
#else
        reply = "{\"Result\":\"4\"}\r\n";
#endif
        StartTx(TX_LITERAL);
    } else if (i == 9u && command[0] == '(' && command[1] == 'S' &&
               command[2] == 'T' && command[3] == 'A' && command[4] == 'R' &&
               command[5] == 'T' && command[6] == ',' && command[8] == ')' &&
               command[7] >= '1' && command[7] <= '4') {
        BeginTest(command[7] - '0');
    } else Fail(APP_ERROR_COMMAND);
}

static void ReceiveCommand(uint8 c)
{
    if (command_discard) {
        if (c == '(') { command_discard = 0u; command_len = 0u; }
        else {
            if (c == ')' || c == '\r' || c == '\n') command_discard = 0u;
            return;
        }
    }
    if (c == '\r' || c == '\n') { ExecuteCommand(); return; }
    if (c < 0x20u && c != '\t') {
        command_len = 0u; command_discard = 1u; Fail(APP_ERROR_COMMAND); return;
    }
    if (command_len == COMMAND_MAX) {
        command_len = 0u; command_discard = 1u; Fail(APP_ERROR_COMMAND); return;
    }
    command[command_len++] = c;
    /* The PC's acknowledgement may be the bare two-byte token OK. */
    if (command_len == 2u && Upper(command[0]) == 'O' && Upper(command[1]) == 'K') {
        command_len = 0u;
        return;
    }
    if (c == ')') ExecuteCommand();
}

static uint8 Marker(uint16 length, const char CODE *s)
{
    uint16 begin, end;
    begin = 0u;
    end = length;
    Trim(&begin, &end);
    while (begin < end && *s) {
        if (Upper(Uart1_Peek(begin++)) != Upper((uint8)*s++)) return 0u;
    }
    return (uint8)(begin == end && *s == '\0');
}

static void PollInput(void)
{
    uint16 available;
    uint8 c, budget;
    available = Uart1_Count();
    budget = 32u;
    while (line_scan < available && budget--) {
        c = Uart1_Peek(line_scan);
        if (c == '\r' || c == '\n') {
            if (Marker(line_scan, "") || Marker(line_scan, "test")) Uart1_Drop(line_scan + 1u);
            else if (Marker(line_scan, "end")) {
                if (app_state == APP_WAIT_FIRST) { Fail(APP_ERROR_FORMAT); return; }
                Uart1_ClearRx();
                app_state = APP_IDLE;
            } else if (ParseRecord(line_scan)) {
                app_state = APP_RECEIVING;
                StartTx(TX_JSON);
            } else { Fail(APP_ERROR_FORMAT); return; }
            line_scan = 0u;
            return;
        }
        if (++line_scan > INPUT_LINE_MAX) { Fail(APP_ERROR_LINE_LONG); return; }
    }
}

void AppComm_Init(void)
{
    app_state = APP_IDLE;
    app_last_error = APP_ERROR_NONE;
    module_id = FIXTURE_DEFAULT_ID;
    start_pos = command_len = command_discard = 0u;
    error_pending = tx_kind = tx_phase = tx_index = tx_escape = 0u;
    byte_pending = tx_finished = 0u;
    line_scan = first_started = 0u;
    Uart1_ClearRx();
    Uart2_ClearRx();
}

void AppComm_Poll(void)
{
    uint8 c;
    if (Uart1_Overflow() && app_state != APP_IDLE) Fail(APP_ERROR_UART1_FULL);
    if (Uart2_Overflow()) {
        Uart2_ClearRx();
        command_len = 0u;
        command_discard = 1u;
        Fail(APP_ERROR_UART2_FULL);
    }
    if (tx_kind) { PollTx(); return; }
    if (error_pending) {
        Uart1_ClearRx();
        line_scan = 0u;
        error_pending = 0u;
        reply = err_text;
        StartTx(TX_LITERAL);
        return;
    }
    if (app_state == APP_SENDING_START) {
        if (start_pos < 4u) c = (uint8)"test"[start_pos];
        else if (start_pos == 4u) c = '0' + module_id;
        else if (start_pos == 5u) c = '\r';
        else c = '\n';
        if (start_pos < 7u) {
            if (Uart1_TryWrite(c)) ++start_pos;
        } else if (Uart1_TxIdle()) {
            first_started = Sys_Millis();
            app_state = APP_WAIT_FIRST;
        }
        return;
    }
    if (Uart2_ReadByte(&c)) {
        ReceiveCommand(c);
        if (tx_kind || error_pending || app_state == APP_SENDING_START) return;
    }
    if (app_state == APP_IDLE) { Uart1_ClearRx(); line_scan = 0u; return; }
    /* Process complete data before testing the deadline in this poll. */
    PollInput();
    if (app_state == APP_WAIT_FIRST &&
        (uint16)(Sys_Millis() - first_started) >= FIRST_RESPONSE_MS) Fail(APP_ERROR_FIRST_TIMEOUT);
}
