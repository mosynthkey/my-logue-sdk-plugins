<script setup>
import { useI18n } from "../composables/useI18n.js";

defineProps({
  plugins: {
    type: Array,
    required: true,
  },
  selectedPluginId: {
    type: String,
    default: null,
  },
});

const emit = defineEmits(["select-plugin"]);
const { locale, setLocale, t } = useI18n();
</script>

<template>
  <aside class="sidebar" :aria-label="t('pluginList')">
    <header class="sidebar__head">
      <h1 class="sidebar__title">Logue SDK<br>Plugins List</h1>
      <div class="sidebar__mobile-controls">
        <div class="plugin-picker">
          <label class="plugin-picker__label" for="plugin-select">{{ t("plugin") }}</label>
          <select
            id="plugin-select"
            class="plugin-picker__select"
            :aria-label="t('selectPlugin')"
            :value="selectedPluginId || ''"
            @change="emit('select-plugin', $event.target.value)"
          >
            <option
              v-for="plugin in plugins"
              :key="plugin.id"
              :value="plugin.id"
            >
              {{ plugin.name }}
            </option>
          </select>
        </div>

        <div class="language-picker language-picker--mobile">
          <label class="plugin-picker__label" for="language-select-mobile">{{ t("language") }}</label>
          <select
            id="language-select-mobile"
            class="plugin-picker__select"
            :value="locale"
            @change="setLocale($event.target.value)"
          >
            <option value="en">English</option>
            <option value="ja">日本語</option>
          </select>
        </div>
      </div>
    </header>

    <nav class="plugin-nav" :aria-label="t('pluginList')">
      <button
        v-for="plugin in plugins"
        :key="plugin.id"
        type="button"
        class="plugin-nav__item"
        :class="{ 'is-active': plugin.id === selectedPluginId }"
        @click="emit('select-plugin', plugin.id)"
      >
        <span>{{ plugin.name }}</span>
      </button>
    </nav>

    <div class="language-picker language-picker--desktop">
      <label class="plugin-picker__label" for="language-select">{{ t("language") }}</label>
      <select
        id="language-select"
        class="plugin-picker__select"
        :value="locale"
        @change="setLocale($event.target.value)"
      >
        <option value="en">English</option>
        <option value="ja">日本語</option>
      </select>
    </div>
  </aside>
</template>
