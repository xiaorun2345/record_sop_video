<script setup lang="ts">
import { ArrowDown, Bell, Cpu, House, QuestionFilled, User } from "@element-plus/icons-vue";
</script>

<template>
  <header class="topbar">
    <div class="page-heading">
      <h1>SOP配置</h1>
      <span class="top-separator"></span>
      <el-icon><House /></el-icon>
      <span>/</span>
      <span>SOP配置</span>
    </div>
    <div class="top-actions">
      <div class="device-chip"><el-icon><Cpu /></el-icon>RK3588 Industrial Vision <span>在线</span></div>
      <button class="icon-button notify" type="button" aria-label="通知">
        <el-icon><Bell /></el-icon><b>3</b>
      </button>
      <button class="icon-button" type="button" aria-label="帮助"><el-icon><QuestionFilled /></el-icon></button>
      <span class="top-divider"></span>
      <div class="account"><span class="avatar"><el-icon><User /></el-icon></span>admin<el-icon><ArrowDown /></el-icon></div>
    </div>
  </header>
</template>
