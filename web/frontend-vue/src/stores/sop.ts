import { computed, ref } from "vue";
import { defineStore } from "pinia";
import { sopApi } from "@/api/sop";
import { persistPublishedSop, removePersistedSop } from "@/api/published-snapshot";
import { createSopDefinition, createSopStep } from "@/domain/sop";
import type { CreateSopInput, SopDefinition, SopExecutionMode, SopValidationIssue, UpdateSopInput } from "@/types/sop";

const emptySop = createSopDefinition("未命名 SOP", "", "ordered");
const emptyStep = createSopStep("", "");

export const useSopStore = defineStore("sop", () => {
  const sops = ref<SopDefinition[]>([]);
  const activeSopId = ref("");
  const activeStepId = ref("");
  const draftChanged = ref(false);
  const loading = ref(false);
  const saving = ref(false);
  const errorMessage = ref("");

  const activeSop = computed<SopDefinition>(() => {
    return sops.value.find((sop) => sop.id === activeSopId.value) ?? sops.value[0] ?? emptySop;
  });
  const steps = computed(() => activeSop.value.steps);
  const activeStep = computed(() => {
    return steps.value.find((step) => step.id === activeStepId.value) ?? steps.value[0] ?? emptyStep;
  });
  async function initialize() {
    if (sops.value.length || loading.value) return;
    loading.value = true;
    errorMessage.value = "";
    try {
      sops.value = await sopApi.list();
      const first = sops.value[0];
      if (first) {
        activeSopId.value = first.id;
        activeStepId.value = first.steps[0]?.id ?? "";
      }
    } catch (error) {
      errorMessage.value = getErrorMessage(error);
      throw error;
    } finally {
      loading.value = false;
    }
  }

  function selectSop(id: string) {
    const target = sops.value.find((sop) => sop.id === id);
    if (!target) return;
    activeSopId.value = id;
    activeStepId.value = target.steps[0]?.id ?? "";
    draftChanged.value = false;
  }

  async function createSop(name: string, description: string, executionMode: SopExecutionMode) {
    return runSaving(async () => {
      const sop = await sopApi.create({ name, description, executionMode } satisfies CreateSopInput);
      sops.value.unshift(sop);
      activeSopId.value = sop.id;
      activeStepId.value = sop.steps[0]?.id ?? "";
      draftChanged.value = true;
      return sop;
    });
  }

  async function updateSop(id: string, name: string, description: string, executionMode: SopExecutionMode) {
    return runSaving(async () => {
      const local = sops.value.find((item) => item.id === id);
      const updated = await sopApi.update({ id, name, description, executionMode } satisfies UpdateSopInput);
      if (local) updated.steps = local.steps;
      replaceSop(updated);
      draftChanged.value = true;
      return updated;
    });
  }

  async function deleteSop(id: string) {
    if (sops.value.length <= 1) return false;
    const index = sops.value.findIndex((sop) => sop.id === id);
    if (index < 0) return false;
    await runSaving(() => sopApi.remove(id));
    removePersistedSop(id);
    sops.value.splice(index, 1);
    const next = sops.value[Math.min(index, sops.value.length - 1)];
    activeSopId.value = next.id;
    activeStepId.value = next.steps[0]?.id ?? "";
    draftChanged.value = false;
    return true;
  }

  function selectStep(id: string) {
    if (steps.value.some((step) => step.id === id)) activeStepId.value = id;
  }

  function markChanged() {
    activeSop.value.status = "draft";
    activeSop.value.version = "草稿";
    draftChanged.value = true;
  }

  function reorder(fromIndex: number, toIndex: number) {
    if (fromIndex === toIndex || fromIndex < 0 || toIndex < 0 || fromIndex >= steps.value.length || toIndex >= steps.value.length) return;
    const [step] = steps.value.splice(fromIndex, 1);
    steps.value.splice(toIndex, 0, step);
    markChanged();
  }

  function createStep(name: string, description: string) {
    const stepNumber = nextStepNumber();
    const step = createSopStep(`STEP-${String(stepNumber).padStart(3, "0")}`, name, { description });
    steps.value.push(step);
    activeStepId.value = step.id;
    markChanged();
    return step;
  }

  function deleteActiveStep() {
    const index = steps.value.findIndex((step) => step.id === activeStepId.value);
    if (index < 0) return false;
    const removedObjectIds = new Set(steps.value[index].requiredObjects.map((object) => object.id));
    steps.value.splice(index, 1);
    steps.value.forEach((step) => {
      step.requiredObjects.forEach((object) => {
        if (object.relation && removedObjectIds.has(object.relation.targetObjectId)) object.relation = null;
      });
    });
    activeStepId.value = steps.value[Math.min(index, steps.value.length - 1)]?.id ?? "";
    markChanged();
    return true;
  }

  function nextStepNumber() {
    const used = new Set(steps.value.map((step) => step.id));
    let number = 1;
    while (used.has(`STEP-${String(number).padStart(3, "0")}`)) number += 1;
    return number;
  }

  async function validateActive(): Promise<SopValidationIssue[]> {
    return sopApi.validate(activeSop.value);
  }

  async function publishActive() {
    return runSaving(async () => {
      const published = await sopApi.publish(activeSop.value);
      const savedLocally = persistPublishedSop(published);
      replaceSop(published);
      draftChanged.value = false;
      return { published, savedLocally };
    });
  }

  function replaceSop(value: SopDefinition) {
    const index = sops.value.findIndex((item) => item.id === value.id);
    if (index >= 0) sops.value.splice(index, 1, value);
    else sops.value.unshift(value);
  }

  async function runSaving<T>(action: () => Promise<T>): Promise<T> {
    saving.value = true;
    errorMessage.value = "";
    try {
      return await action();
    } catch (error) {
      errorMessage.value = getErrorMessage(error);
      throw error;
    } finally {
      saving.value = false;
    }
  }

  return {
    sops,
    activeSop,
    activeSopId,
    steps,
    activeStep,
    activeStepId,
    draftChanged,
    loading,
    saving,
    errorMessage,
    initialize,
    selectSop,
    createSop,
    updateSop,
    deleteSop,
    selectStep,
    markChanged,
    reorder,
    createStep,
    deleteActiveStep,
    validateActive,
    publishActive,
  };
});

function getErrorMessage(error: unknown): string {
  return error instanceof Error ? error.message : "SOP 请求失败，请稍后重试";
}
