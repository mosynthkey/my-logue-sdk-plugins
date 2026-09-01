import { onMounted, ref } from "vue";

export function useCatalog() {
  const catalog = ref(null);
  const loadError = ref(null);
  const loading = ref(true);

  async function loadCatalog() {
    loading.value = true;
    loadError.value = null;

    try {
      const response = await fetch("./catalog.json", { cache: "no-store" });
      if (!response.ok) {
        throw new Error("catalog.json is missing. Build the site first.");
      }
      catalog.value = await response.json();
    } catch (error) {
      loadError.value = error.message;
      catalog.value = null;
    } finally {
      loading.value = false;
    }
  }

  onMounted(loadCatalog);

  return {
    catalog,
    loadError,
    loading,
    loadCatalog,
  };
}
