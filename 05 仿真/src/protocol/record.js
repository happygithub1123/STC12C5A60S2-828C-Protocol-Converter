export const LIMITS = Object.freeze({ line: 511, name: 31, unit: 8, command: 23 });
export const encoder = new TextEncoder();
export const bytes = value => value instanceof Uint8Array ? value : encoder.encode(value);
export const trim = value => value.replace(/^[ \t]+|[ \t]+$/g, '');
const number = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/;
const validText = value => !/[\x00-\x1f\x7f]/.test(value);

/** 对整条记录校验，保留原数值文本，只编码首组。长度按 UTF-8 字节计。 */
export function parseRecord(input, module) {
  const data = bytes(input);
  if (data.length > LIMITS.line) throw new Error('单行超过 511 字节');
  const line = new TextDecoder('utf-8', { fatal: true }).decode(data);
  const match = /^([^,]*),([^,]*),([^,]*),([^,]*),[ \t]*\(([^)]*)\)[ \t]*,([\s\S]*)$/.exec(line);
  if (!match) throw new Error('记录结构错误');
  const [name, upper, lower, unit, body, resultText] = match.slice(1).map(trim);
  if (!name || ['MH', 'Result'].includes(name) || !validText(name) || bytes(name).length > LIMITS.name) throw new Error('项目名无效或过长');
  if (!validText(unit) || bytes(unit).length > LIMITS.unit) throw new Error('单位无效或过长');
  if (!number.test(upper) || !number.test(lower)) throw new Error('上下限不是有效数值');
  const result = resultText.toUpperCase();
  if (!['OK', 'NG'].includes(result)) throw new Error('末尾结果必须为 OK / NG');
  const measurements = [];
  if (body) {
    const parts = body.split(',');
    if (trim(parts.at(-1)) === '') parts.pop();
    for (const part of parts) {
      const item = trim(part);
      const eq = item.indexOf('=');
      const point = trim(item.slice(0, eq));
      const value = trim(item.slice(eq + 1));
      if (eq < 1 || !point || !validText(point) || /[()]/.test(point) || !number.test(value)) throw new Error('测点组格式或数值错误（所有组均须合法）');
      measurements.push({ point, value });
    }
    if (!measurements.length) throw new Error('测点组为空');
  }
  const first = measurements[0];
  const selected = first ? `${first.point}=${first.value}${unit ? ` ${unit}` : ''}:${result}` : result;
  const raw = `{"MH":${JSON.stringify(String(module))},"Result":${JSON.stringify(result)},${JSON.stringify(name)}:${JSON.stringify(selected)}}\r\n`;
  return { line, name, upper, lower, unit, result, first, groupCount: measurements.length, selected, raw };
}

export function getModule(mask) {
  return ({ 1: 1, 2: 2, 4: 3, 8: 4 })[mask & 15] || 0;
}
