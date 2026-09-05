<script setup lang="ts">
import {
  ArrowRight,
  Bell,
  Box,
  Coin,
  Document,
  Grid,
  House,
  Monitor,
  Operation,
  Setting,
} from "@element-plus/icons-vue";
import { RouterLink } from "vue-router";

const navigation = [
  { label: "工作台", icon: House, path: "/" },
  { label: "设备管理", icon: Monitor, path: "/device", child: true },
  { label: "模型管理", icon: Box, child: true },
  { label: "应用管理", icon: Grid, child: true },
  { label: "数据管理", icon: Coin, child: true },
  { label: "告警管理", icon: Bell, child: true },
  { label: "日志管理", icon: Document, child: true },
  { label: "SOP配置", icon: Operation, path: "/sop-config" },
  { label: "系统设置", icon: Setting, path: "/settings" },
];
</script>

<template>
  <aside class="sidebar">
    <div class="brand-mark" aria-label="DenseAI">
      <span class="brand-symbol">D</span>
      <span>DenseAI</span>
    </div>
    <div class="console-label">EDGE CONSOLE</div>

    <nav class="side-navigation" aria-label="主导航">
      <RouterLink
        v-for="item in navigation"
        :key="item.label"
        class="nav-item"
        :to="item.path || '#'"
        :class="{ active: item.path === '/sop-config' }"
      >
        <el-icon :size="20"><component :is="item.icon" /></el-icon>
        <span>{{ item.label }}</span>
        <el-icon v-if="item.child" class="nav-arrow"><ArrowRight /></el-icon>
      </RouterLink>
    </nav>

    <div class="device-card">
      <div class="meta-label">当前设备</div>
      <strong>RK3588 Industrial Vision</strong>
      <div class="online-row"><span class="status-dot"></span>在线</div>
      <div class="device-meta">设备ID：RK3588-IV-001</div>
      <div class="device-meta">固件版本：v1.2.3</div>
    </div>
    <button class="collapse-button" type="button"><span>«</span> 收起菜单</button>
  </aside>
</template>
