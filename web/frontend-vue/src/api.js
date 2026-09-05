export const api = (new URLSearchParams(location.search).get('api') || location.origin).replace(/\/$/, '');
export async function request(path, options = {}) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 15000);
  try {
    const response = await fetch(api + path, { ...options, signal: options.signal || controller.signal });
    const data = await response.json().catch(() => { throw new Error('服务返回格式异常，请刷新页面后重试'); });
    if (!response.ok) {
      const detail = data.error || data.detail || data.message;
      throw new Error(Array.isArray(detail) ? detail.map(item => `${item.loc?.slice(1).join('.') || '参数'}：${item.msg}`).join('；') : typeof detail === 'string' ? detail : `请求失败（HTTP ${response.status}）`);
    }
    return data;
  } catch (error) {
    if (error.name === 'AbortError') throw new Error('请求超时，请检查设备连接后重试');
    if (error instanceof TypeError) throw new Error('无法连接设备，请检查网络后重试');
    throw error;
  } finally { clearTimeout(timer); }
}
export function post(path, body = {}) { return request(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }); }
export function put(path, body) { return request(path, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }); }
