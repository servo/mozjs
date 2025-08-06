/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const mozilla = require("eslint-plugin-mozilla");
const globalIgnores = require("./.eslintrc-ignores.js");
const { testPaths } = require("./.eslintrc-test-paths.js");
const { rollouts } = require("./.eslintrc-rollouts.js");
const fs = require("fs");
const path = require("path");

function readFile(filePath) {
  return fs
    .readFileSync(filePath, { encoding: "utf-8" })
    .split("\n")
    .filter(p => p && !p.startsWith("#"));
}

const ignorePatterns = [
  ...globalIgnores,
  ...readFile(
    path.join(__dirname, "tools", "rewriting", "ThirdPartyPaths.txt")
  ),
  ...readFile(path.join(__dirname, "tools", "rewriting", "Generated.txt")),
  ...readFile(
    path.join(
      __dirname,
      "devtools",
      "client",
      "debugger",
      "src",
      ".eslintignore"
    )
  ).map(p => `devtools/client/debugger/src/${p}`),
];
const httpTestingPaths = [
  "**/*mixedcontent",
  "**/*CrossOrigin",
  "**/*crossorigin",
  "**/*cors",
  "**/*downgrade",
  "**/*Downgrade",
];

module.exports = {
  settings: {
    "import/extensions": [".mjs"],
    "import/resolver": {
      [path.resolve(__dirname, "srcdir-resolver.js")]: {},
      node: {},
    },
  },
  ignorePatterns,
  // Ignore eslint configurations in parent directories.
  root: true,
  env: {
    es2024: true,
  },
  // New rules and configurations should generally be added in
  // tools/lint/eslint/eslint-plugin-mozilla/lib/configs/recommended.js to
  // allow external repositories that use the plugin to pick them up as well.
  extends: [
    "plugin:mozilla/recommended",
    "plugin:json/recommended-with-comments-legacy",
    "prettier",
  ],
  plugins: ["mozilla", "html", "import", "json", "promise", "lit"],
  rules: {
    "lit/quoted-expressions": ["error", "never"],
  },
  overrides: [
    {
      files: ["*.*"],
      // The browser environment is not available for system modules, sjs, workers
      // or any of the xpcshell-test files.
      excludedFiles: [
        "*.sys.mjs",
        "*.sjs",
        "**/?(*.)worker.?(m)js",
        ...testPaths.xpcshell.map(filePath => `${filePath}**`),
      ],
      env: {
        browser: true,
      },
    },
    {
      files: ["*.*"],
      env: {
        "mozilla/privileged": true,
        "mozilla/specific": true,
      },
      rules: {
        // Require braces around blocks that start a new line. This must be
        // configured after eslint-config-prettier is included (via `extends`
        // above), as otherwise that configuration disables it. Hence, we do
        // not include it in
        // `tools/lint/eslint/eslint-plugin-mozilla/lib/configs/recommended.js`.
        curly: ["error", "all"],
      },
    },
    {
      files: [
        // All .eslintrc.js files are in the node environment, so turn that
        // on here.
        // https://github.com/eslint/eslint/issues/13008
        ".eslintrc*.js",
        // *.config.js files are generally assumed to be configuration files
        // based for node.
        "*.config.?(m)js",
        // The resolver for moz-src for eslint, vscode etc.
        "srcdir-resolver.js",
      ],
      env: {
        node: true,
        browser: false,
      },
    },
    {
      files: ["browser/base/content/browser.js"],
      rules: {
        "mozilla/no-more-globals": "error",
      },
    },
    {
      files: [
        "**/*.jsx",
        "browser/components/pocket/content/**/*.js",
        "browser/components/storybook/.storybook/**/*.mjs",
      ],
      parserOptions: {
        ecmaFeatures: {
          jsx: true,
        },
      },
    },
    {
      files: ["browser/components/storybook/**"],
      env: {
        "mozilla/privileged": false,
      },
    },
    {
      files: ["*.mjs"],
      rules: {
        "import/default": "error",
        "import/export": "error",
        "import/named": "error",
        "import/namespace": "error",
        "import/newline-after-import": "error",
        "import/no-duplicates": "error",
        "import/no-absolute-path": "error",
        "import/no-named-default": "error",
        "import/no-named-as-default": "error",
        "import/no-named-as-default-member": "error",
        "import/no-self-import": "error",
        "import/no-unassigned-import": "error",
        "import/no-unresolved": [
          "error",
          // Bug 1773473 - Ignore resolver URLs for chrome and resource as we
          // do not yet have a resolver for them.
          { ignore: ["chrome://", "resource://"] },
        ],
        "import/no-useless-path-segments": "error",
      },
    },
    {
      // Turn off no-unassigned-import for files that typically test our
      // custom elements, which are imported for the side effects (ie
      // the custom element being registered) rather than any particular
      // export:
      files: ["**/*.stories.mjs"],
      rules: {
        "import/no-unassigned-import": "off",
      },
    },
    {
      files: ["**/test/**", "**/tests/**"],
      extends: ["plugin:mozilla/general-test"],
    },
    {
      ...mozilla.configs["xpcshell-test"],
      files: testPaths.xpcshell.map(filePath => `${filePath}**`),
      excludedFiles: ["**/*.mjs", "**/*.sjs"],
    },
    {
      // If it is an xpcshell head file, we turn off global unused variable checks, as it
      // would require searching the other test files to know if they are used or not.
      // This would be expensive and slow, and it isn't worth it for head files.
      // We could get developers to declare as exported, but that doesn't seem worth it.
      files: testPaths.xpcshell.map(filePath => `${filePath}head*.js`),
      rules: {
        "no-unused-vars": [
          "error",
          {
            argsIgnorePattern: "^_",
            vars: "local",
          },
        ],
      },
    },
    {
      // This section enables errors of no-unused-vars globally for all test*.js
      // files in xpcshell test paths.
      // This is not done in the xpcshell-test configuration as we cannot pull
      // in overrides from there. We should at some stage, aim to enable this
      // for all files in xpcshell-tests.
      files: testPaths.xpcshell.map(filePath => `${filePath}test*.js`),
      rules: {
        // No declaring variables that are never used
        "no-unused-vars": [
          "error",
          {
            argsIgnorePattern: "^_",
            vars: "all",
          },
        ],
      },
    },
    {
      ...mozilla.configs["browser-test"],
      files: testPaths.browser.map(filePath => `${filePath}**`),
      excludedFiles: ["**/*.mjs", "**/*.sjs"],
    },
    {
      ...mozilla.configs["mochitest-test"],
      files: testPaths.mochitest.map(filePath => `${filePath}**`),
      excludedFiles: [
        "**/*.mjs",
        "security/manager/ssl/tests/mochitest/browser/**",
      ],
    },
    {
      ...mozilla.configs["chrome-test"],
      files: testPaths.chrome.map(filePath => `${filePath}**`),
      excludedFiles: ["**/*.mjs", "**/*.sjs"],
    },
    {
      env: {
        // Ideally we wouldn't be using the simpletest env here, but our uses of
        // js files mean we pick up everything from the global scope, which could
        // be any one of a number of html files. So we just allow the basics...
        "mozilla/simpletest": true,
      },
      files: [
        ...testPaths.mochitest.map(filePath => `${filePath}/**/*.js`),
        ...testPaths.chrome.map(filePath => `${filePath}/**/*.js`),
      ],
      excludedFiles: ["**/*.mjs", "**/*.sjs"],
    },
    {
      // Some directories have multiple kinds of tests, and some rules
      // don't work well for HTML-based mochitests, so disable those.
      files: testPaths.xpcshell
        .concat(testPaths.browser)
        .map(filePath => [`${filePath}/**/*.html`, `${filePath}/**/*.xhtml`])
        .flat(),
      rules: {
        // plain/chrome mochitests don't automatically include Assert, so
        // autofixing `ok()` to Assert.something is bad.
        "mozilla/no-comparison-or-assignment-inside-ok": "off",
      },
    },
    {
      // Some directories reuse `test_foo.js` files between mochitest-plain and
      // unit tests, or use custom postMessage-based assertion propagation into
      // browser tests. Ignore those too:
      files: [
        // Reuses xpcshell unit test scripts in mochitest-plain HTML files.
        "dom/indexedDB/test/**",
        // Dispatches functions to the webpage in ways that are hard to detect.
        "toolkit/components/antitracking/test/**",
      ],
      rules: {
        "mozilla/no-comparison-or-assignment-inside-ok": "off",
      },
    },
    {
      // Rules of Hooks broadly checks for camelCase "use" identifiers, so
      // enable only for paths actually using React to avoid false positives.
      extends: ["plugin:react-hooks/recommended"],
      files: [
        "browser/components/aboutwelcome/**",
        "browser/components/asrouter/**",
        "browser/components/pocket/**",
        "browser/extensions/newtab/**",
        "devtools/**",
      ],
      rules: {
        // react-hooks/recommended has exhaustive-deps as a warning, we prefer
        // errors, so that raised issues get addressed one way or the other.
        "react-hooks/exhaustive-deps": "error",
      },
    },
    {
      // Exempt files with these paths since they have to use http for full coverage
      files: httpTestingPaths.map(filePath => `${filePath}**`),
      rules: {
        "@microsoft/sdl/no-insecure-url": "off",
      },
    },
    ...rollouts,
  ],
};
