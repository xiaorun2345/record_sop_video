<script setup lang="ts">
import type { SopDefinition } from "@/types/sop";

defineProps<{
  sop: SopDefinition;
}>();
</script>

<template>
  <section class="sop-summary-panel panel">
    <div class="summary-heading">
      <div>
        <span>当前流程</span>
        <h3>{{ sop.name }}</h3>
      </div>
      <span class="sop-state" :class="sop.status">{{ sop.status === "published" ? "已发布" : "草稿" }}</span>
    </div>
    <p>{{ sop.description || "暂未填写流程说明。" }}</p>
    <dl class="sop-summary-list">
      <div><dt>流程编号</dt><dd>{{ sop.id }}</dd></div>
      <div><dt>当前版本</dt><dd>{{ sop.version }}</dd></div>
      <div><dt>执行顺序</dt><dd>{{ sop.executionMode === "ordered" ? "有序" : "无序" }}</dd></div>
      <div><dt>步骤数量</dt><dd>{{ sop.steps.length }} 个</dd></div>
      <div><dt>最后更新</dt><dd>{{ sop.updatedAt }}</dd></div>
    </dl>
  </section>
</template>
