<script setup>
defineProps({
  routingId: {
    type: String,
    required: true,
  },
});
</script>

<template>
  <svg
    class="routing-diagram"
    viewBox="0 0 88 40"
    aria-hidden="true"
  >
    <g v-if="routingId === 'serial'">
      <path d="M2 20 H8" />
      <rect x="8" y="14" width="12" height="12" rx="1" />
      <text x="14" y="20">1</text>
      <path d="M20 20 H28" />
      <rect x="28" y="14" width="12" height="12" rx="1" />
      <text x="34" y="20">2</text>
      <path d="M40 20 H48" />
      <rect x="48" y="14" width="12" height="12" rx="1" />
      <text x="54" y="20">3</text>
      <path d="M60 20 H68" />
      <rect x="68" y="14" width="12" height="12" rx="1" />
      <text x="74" y="20">4</text>
      <path d="M80 20 H86" />
      <path d="M84 17 L87 20 L84 23" />
    </g>

    <g v-else-if="routingId === 'serpar'">
      <path d="M2 20 H8" />
      <rect x="8" y="14" width="12" height="12" rx="1" />
      <text x="14" y="20">1</text>
      <path d="M20 20 H28" />
      <rect x="28" y="14" width="12" height="12" rx="1" />
      <text x="34" y="20">2</text>
      <path d="M40 20 H46 M46 20 V10 H52 M46 20 V30 H52" />
      <rect x="52" y="4" width="12" height="12" rx="1" />
      <text x="58" y="10">3</text>
      <rect x="52" y="24" width="12" height="12" rx="1" />
      <text x="58" y="30">4</text>
      <path d="M64 10 H70 V20 H78 M64 30 H70 V20" />
      <path d="M78 20 H86" />
      <path d="M84 17 L87 20 L84 23" />
    </g>

    <g v-else-if="routingId === 'parser'">
      <path d="M2 20 H10 M10 20 V10 H16 M10 20 V30 H16" />
      <rect x="16" y="4" width="12" height="12" rx="1" />
      <text x="22" y="10">1</text>
      <rect x="16" y="24" width="12" height="12" rx="1" />
      <text x="22" y="30">2</text>
      <path d="M28 10 H34 V20 H40 M28 30 H34 V20" />
      <rect x="40" y="14" width="12" height="12" rx="1" />
      <text x="46" y="20">3</text>
      <path d="M52 20 H60" />
      <rect x="60" y="14" width="12" height="12" rx="1" />
      <text x="66" y="20">4</text>
      <path d="M72 20 H86" />
      <path d="M84 17 L87 20 L84 23" />
    </g>

    <g v-else-if="routingId === '1para'">
      <path d="M2 20 H8 M8 20 V10 H14 M8 20 V30 H22" />
      <rect x="14" y="4" width="12" height="12" rx="1" />
      <text x="20" y="10">1</text>
      <path d="M26 10 H34" />
      <rect x="34" y="4" width="12" height="12" rx="1" />
      <text x="40" y="10">2</text>
      <path d="M46 10 H54" />
      <rect x="54" y="4" width="12" height="12" rx="1" />
      <text x="60" y="10">3</text>
      <rect x="22" y="24" width="12" height="12" rx="1" />
      <text x="28" y="30">4</text>
      <path d="M34 30 H66 V20 H74 M66 10 V20" />
      <path d="M74 20 H86" />
      <path d="M84 17 L87 20 L84 23" />
    </g>

    <g v-else-if="routingId === '2by2'">
      <path d="M2 20 H10 M10 20 V10 H18 M10 20 V30 H18" />
      <rect x="18" y="4" width="12" height="12" rx="1" />
      <text x="24" y="10">1</text>
      <path d="M30 10 H40" />
      <rect x="40" y="4" width="12" height="12" rx="1" />
      <text x="46" y="10">2</text>
      <rect x="18" y="24" width="12" height="12" rx="1" />
      <text x="24" y="30">3</text>
      <path d="M30 30 H40" />
      <rect x="40" y="24" width="12" height="12" rx="1" />
      <text x="46" y="30">4</text>
      <path d="M52 10 H60 V20 H74 M52 30 H60 V20" />
      <path d="M74 20 H86" />
      <path d="M84 17 L87 20 L84 23" />
    </g>

    <g v-else-if="routingId === '3to1'">
      <path d="M2 20 H8 M8 20 V8 H16 M8 20 H16 M8 20 V32 H16" />
      <rect x="16" y="2" width="12" height="12" rx="1" />
      <text x="22" y="8">1</text>
      <rect x="16" y="14" width="12" height="12" rx="1" />
      <text x="22" y="20">2</text>
      <rect x="16" y="26" width="12" height="12" rx="1" />
      <text x="22" y="32">3</text>
      <path d="M28 8 H36 V20 H46 M28 20 H46 M28 32 H36 V20" />
      <rect x="46" y="14" width="12" height="12" rx="1" />
      <text x="52" y="20">4</text>
      <path d="M58 20 H86" />
      <path d="M84 17 L87 20 L84 23" />
    </g>
  </svg>
</template>
