import "@mdi/font/css/materialdesignicons.css";
import "vuetify/styles";
import { createVuetify } from "vuetify";
import { md3 } from "vuetify/blueprints";
import { aliases, mdi } from "vuetify/iconsets/mdi";

export default createVuetify({
  blueprint: md3,
  theme: {
    defaultTheme: "dark",
    themes: {
      dark: {
        dark: true,
        colors: {
          background: "#121212",
          surface: "#1e1e1e",
          primary: "#b6b2a1",
          secondary: "#8f8b7c",
          error: "#ff3333",
          warning: "#ffb020",
          success: "#4caf50",
          info: "#64b5f6",
        },
      },
    },
  },
  icons: {
    defaultSet: "mdi",
    aliases,
    sets: { mdi },
  },
  defaults: {
    VBtn: {
      rounded: "lg",
    },
    VCard: {
      rounded: "lg",
    },
    VDialog: {
      scrim: true,
    },
  },
});
