import assert from "node:assert/strict";
import test from "node:test";
import {
  buildUnitsZipBytes,
  unitZipEntries,
  zipFolderName,
} from "../website/src/utils/downloadUnitsZip.js";
import { listStoreZipPaths } from "../website/src/utils/zipStore.js";

const plugins = [
  {
    id: "scenesabot",
    name: "SceneSabot",
    builds: [
      { target: "nts-3_kaoss", file: "./units/scenesabot-nts-3_kaoss.nts3unit" },
    ],
  },
  {
    id: "hypersaw",
    name: "HyperSaw",
    builds: [
      { target: "nts-1_mkii", file: "./units/hypersaw-nts-1_mkii.nts1mkiiunit" },
      { target: "microkorg2", file: "./units/hypersaw-microkorg2.mk2unit" },
    ],
  },
];

test("zip entries are grouped by effector with one file per target", () => {
  assert.deepEqual(unitZipEntries(plugins), [
    {
      path: "SceneSabot/scenesabot-nts-3_kaoss.nts3unit",
      url: "./units/scenesabot-nts-3_kaoss.nts3unit",
    },
    {
      path: "HyperSaw/hypersaw-nts-1_mkii.nts1mkiiunit",
      url: "./units/hypersaw-nts-1_mkii.nts1mkiiunit",
    },
    {
      path: "HyperSaw/hypersaw-microkorg2.mk2unit",
      url: "./units/hypersaw-microkorg2.mk2unit",
    },
  ]);
});

test("folder names drop path separators", () => {
  assert.equal(zipFolderName({ id: "x", name: "A/B\\C" }), "A_B_C");
});

test("built zip stores each target file under its effector folder", async () => {
  const files = new Map([
    ["./units/scenesabot-nts-3_kaoss.nts3unit", new Uint8Array([1, 2, 3])],
    ["./units/hypersaw-nts-1_mkii.nts1mkiiunit", new Uint8Array([4, 5])],
    ["./units/hypersaw-microkorg2.mk2unit", new Uint8Array([6])],
  ]);

  const zipBytes = await buildUnitsZipBytes(plugins, async (url) => ({
    ok: true,
    arrayBuffer: async () => files.get(url).buffer,
  }));

  assert.deepEqual(listStoreZipPaths(zipBytes), [
    "SceneSabot/scenesabot-nts-3_kaoss.nts3unit",
    "HyperSaw/hypersaw-nts-1_mkii.nts1mkiiunit",
    "HyperSaw/hypersaw-microkorg2.mk2unit",
  ]);
});
