<script setup>
import LanguageLabel from "./LanguageLabel.vue";
import { useI18n } from "../composables/useI18n.js";
import { targetName } from "../utils/plugin.js";

defineProps({
  plugins: {
    type: Array,
    required: true,
  },
  selectedPluginId: {
    type: String,
    default: null,
  },
  downloadAllBusy: {
    type: Boolean,
    default: false,
  },
  downloadAllDisabled: {
    type: Boolean,
    default: false,
  },
  downloadAllError: {
    type: String,
    default: "",
  },
});

const emit = defineEmits(["select-plugin", "download-all"]);
const { locale, setLocale, t } = useI18n();

function pluginTargets(plugin) {
  if (Array.isArray(plugin.targets) && plugin.targets.length > 0) {
    return plugin.targets;
  }
  return (plugin.builds || []).map((build) => build.target);
}
</script>

<template>
  <aside class="sidebar" :aria-label="t('pluginList')">
    <header class="sidebar__head">
      <h1 class="sidebar__title">My Logue SDK<br>Plugins</h1>
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
          <LanguageLabel for-id="language-select-mobile" :text="t('language')" />
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

      <div class="sidebar__download">
        <button
          type="button"
          class="sidebar__download-all"
          :disabled="downloadAllDisabled || downloadAllBusy"
          @click="emit('download-all')"
        >
          <svg class="device-button__icon" viewBox="0 0 24 24" aria-hidden="true">
            <path d="M12 3v12" />
            <path d="m7 11 5 5 5-5" />
            <path d="M5 19h14" />
          </svg>
          <span>{{ downloadAllBusy ? t("downloadAllBusy") : t("downloadAll") }}</span>
        </button>
        <p v-if="downloadAllError" class="sidebar__download-error">{{ downloadAllError }}</p>
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
        <span class="plugin-nav__name">{{ plugin.name }}</span>
        <span class="plugin-nav__targets" aria-hidden="true">
          <span
            v-for="target in pluginTargets(plugin)"
            :key="target"
            class="plugin-nav__pill"
          >
            {{ targetName(target) }}
          </span>
        </span>
      </button>
    </nav>

    <div class="language-picker language-picker--desktop">
      <LanguageLabel for-id="language-select" :text="t('language')" />
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
