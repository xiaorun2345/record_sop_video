import type { SopDefinition } from "@/types/sop";

const STORAGE_KEY = "denseai.sop.published.v1";

interface StoredSopState {
  schemaVersion: 1;
  lastPublishedSopId: string;
  sops: SopDefinition[];
}

export function persistPublishedSop(sop: SopDefinition): boolean {
  if (!hasLocalStorage()) return false;
  const current = readState();
  const remaining = (current?.sops ?? []).filter((item) => item.id !== sop.id);
  const state: StoredSopState = {
    schemaVersion: 1,
    lastPublishedSopId: sop.id,
    sops: [cloneJson(sop), ...remaining],
  };

  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    return true;
  } catch {
    return false;
  }
}

export function removePersistedSop(sopId: string): void {
  if (!hasLocalStorage()) return;
  const current = readState();
  if (!current || !current.sops.some((sop) => sop.id === sopId)) return;
  const sops = current.sops.filter((sop) => sop.id !== sopId);

  try {
    if (!sops.length) {
      window.localStorage.removeItem(STORAGE_KEY);
      return;
    }
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify({
      schemaVersion: 1,
      lastPublishedSopId: current.lastPublishedSopId === sopId ? sops[0].id : current.lastPublishedSopId,
      sops,
    } satisfies StoredSopState));
  } catch {
    // 本地快照是后端持久化的冗余副本，写入失败不影响后端删除结果。
  }
}

function readState(): StoredSopState | null {
  if (!hasLocalStorage()) return null;
  try {
    const value: unknown = JSON.parse(window.localStorage.getItem(STORAGE_KEY) ?? "null");
    return isStoredSopState(value) ? value : null;
  } catch {
    return null;
  }
}

function isStoredSopState(value: unknown): value is StoredSopState {
  if (!value || typeof value !== "object") return false;
  const state = value as Partial<StoredSopState>;
  return state.schemaVersion === 1
    && typeof state.lastPublishedSopId === "string"
    && Array.isArray(state.sops)
    && state.sops.every((sop) => sop && typeof sop === "object" && typeof sop.id === "string");
}

function hasLocalStorage(): boolean {
  return typeof window !== "undefined" && typeof window.localStorage !== "undefined";
}

function cloneJson<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}
