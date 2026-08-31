<script setup>
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
</script>

<template>
  <aside class="sidebar" aria-label="Plugin list">
    <header class="sidebar__head">
      <h1 class="sidebar__title">Logue SDK<br>Plugins List</h1>
      <div class="plugin-picker">
        <label class="plugin-picker__label" for="plugin-select">Plugin</label>
        <select
          id="plugin-select"
          class="plugin-picker__select"
          aria-label="Select plugin"
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
    </header>

    <nav class="plugin-nav" aria-label="Plugins">
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
  </aside>
</template>
