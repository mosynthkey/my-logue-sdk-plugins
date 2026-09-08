<script setup>
import { computed } from "vue";
import { useDisplay } from "vuetify";
import { useI18n } from "../composables/useI18n.js";
import { PLUGIN_CATEGORIES, categoryMessageKey } from "../utils/pluginCategory.js";
import { targetName } from "../utils/plugin.js";

const props = defineProps({
  modelValue: {
    type: Boolean,
    default: true,
  },
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

const emit = defineEmits(["update:modelValue", "select-plugin", "select-category"]);
const { locale, setLocale, t } = useI18n();
const { mdAndUp } = useDisplay();
const categories = ["all", ...PLUGIN_CATEGORIES];

const drawer = computed({
  get: () => props.modelValue,
  set: (value) => emit("update:modelValue", value),
});

const categoryItems = computed(() => categories.map((category) => ({
  value: category,
  title: categoryLabel(category),
})));

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
  <v-navigation-drawer
    v-model="drawer"
    :permanent="mdAndUp"
    :temporary="!mdAndUp"
    width="280"
  >
    <div class="pa-4 pb-2">
      <v-select
        :model-value="selectedCategory"
        :items="categoryItems"
        item-title="title"
        item-value="value"
        :label="t('category')"
        density="compact"
        hide-details
        @update:model-value="emit('select-category', $event)"
      />
    </div>

    <v-list
      density="comfortable"
      nav
      class="flex-grow-1 overflow-y-auto"
    >
      <v-list-subheader>{{ t("pluginList") }}</v-list-subheader>
      <v-list-item
        v-for="plugin in plugins"
        :key="plugin.id"
        :title="plugin.name"
        :subtitle="pluginTargets(plugin).map(targetName).join(' · ')"
        :active="selectedPluginId === plugin.id"
        rounded="lg"
        @click="emit('select-plugin', plugin.id)"
      />
      <v-list-item
        v-if="!plugins.length"
        :title="t('noPluginsInCategory')"
        disabled
      />
    </v-list>

    <template #append>
      <div class="pa-4">
        <v-select
          :model-value="locale"
          :items="[
            { title: 'English', value: 'en' },
            { title: '日本語', value: 'ja' },
          ]"
          :label="t('language')"
          density="compact"
          hide-details
          @update:model-value="setLocale"
        />
      </div>
    </template>
  </v-navigation-drawer>
</template>
