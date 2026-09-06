<script setup>
import LanguageLabel from "./LanguageLabel.vue";
import { useI18n } from "../composables/useI18n.js";
import { PLUGIN_CATEGORIES, categoryMessageKey } from "../utils/pluginCategory.js";
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
  selectedCategory: {
    type: String,
    default: "all",
  },
});

const emit = defineEmits(["select-plugin", "select-category"]);
const { locale, setLocale, t } = useI18n();
const categories = ["all", ...PLUGIN_CATEGORIES];

function pluginTargets(plugin) {
  if (Array.isArray(plugin.targets) && plugin.targets.length > 0) {
    return plugin.targets;
  }
  return (plugin.builds || []).map((build) => build.target);
}

function categoryLabel(category) {
  const messageKey = categoryMessageKey(category);
  return messageKey ? t(messageKey) : category;
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
    </header>

    <div class="sidebar__filters">
      <div
        class="category-filter category-filter--desktop"
        role="group"
        :aria-label="t('category')"
      >
        <span class="plugin-picker__label">{{ t("category") }}</span>
        <div class="category-filter__chips">
          <button
            v-for="category in categories"
            :key="category"
            type="button"
            class="category-filter__chip"
            :class="{ 'is-active': category === selectedCategory }"
            :aria-pressed="category === selectedCategory"
            @click="emit('select-category', category)"
          >
            {{ categoryLabel(category) }}
          </button>
        </div>
      </div>

      <div class="category-filter category-filter--mobile">
        <label class="plugin-picker__label" for="category-select">{{ t("category") }}</label>
        <select
          id="category-select"
          class="plugin-picker__select"
          :aria-label="t('selectCategory')"
          :value="selectedCategory"
          @change="emit('select-category', $event.target.value)"
        >
          <option
            v-for="category in categories"
            :key="category"
            :value="category"
          >
            {{ categoryLabel(category) }}
          </option>
        </select>
      </div>
    </div>

    <nav class="plugin-nav" :aria-label="t('pluginList')">
      <p
        v-if="plugins.length === 0"
        class="plugin-nav__empty"
      >
        {{ t("noPluginsInCategory") }}
      </p>
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
