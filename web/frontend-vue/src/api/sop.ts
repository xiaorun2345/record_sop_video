import { requestJson } from "@/api/http";
import type { CreateSopInput, SopDefinition, SopValidationIssue, UpdateSopInput } from "@/types/sop";

export interface SopApi {
  list(): Promise<SopDefinition[]>;
  create(input: CreateSopInput): Promise<SopDefinition>;
  update(input: UpdateSopInput): Promise<SopDefinition>;
  remove(id: string): Promise<void>;
  validate(sop: SopDefinition): Promise<SopValidationIssue[]>;
  publish(sop: SopDefinition): Promise<SopDefinition>;
}

export const sopApi: SopApi = {
  list() {
    return requestJson<SopDefinition[]>("/sops");
  },

  create(input: CreateSopInput) {
    return requestJson<SopDefinition>("/sops", { method: "POST", body: JSON.stringify(input) });
  },

  update(input: UpdateSopInput) {
    return requestJson<SopDefinition>(`/sops/${encodeURIComponent(input.id)}`, {
      method: "PATCH",
      body: JSON.stringify({
        name: input.name,
        description: input.description,
        executionMode: input.executionMode,
      }),
    });
  },

  async remove(id: string) {
    await requestJson<unknown>(`/sops/${encodeURIComponent(id)}`, { method: "DELETE" });
  },

  validate(sop: SopDefinition) {
    return requestJson<SopValidationIssue[]>(`/sops/${encodeURIComponent(sop.id)}/validate`, {
      method: "POST",
      body: JSON.stringify(sop),
    });
  },

  publish(sop: SopDefinition) {
    return requestJson<SopDefinition>(`/sops/${encodeURIComponent(sop.id)}/publish`, {
      method: "POST",
      body: JSON.stringify(sop),
    });
  },
};
