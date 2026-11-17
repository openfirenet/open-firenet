# Translating Open Firenet

Thank you for helping! Open Firenet is in English, French and German. Adding a language needs no programming: it is a
matter of copying the English texts and translating them.

There are three places with texts, in three repositories. They are independent: translate one, two or all three, in
separate pull requests.

| What | Where | Size |
|---|---|---|
| The web page of the bridge | this repository, `open-firenet/web/index.html` | about 250 texts |
| The Home Assistant integration | [open-firenet-ha](https://github.com/openfirenet/open-firenet-ha), `custom_components/open_firenet/translations/` | about 60 texts |
| The installer | [open-firenet-installer](https://github.com/openfirenet/open-firenet-installer), `src/i18n.js` and `src-tauri/src/cli_i18n.rs` | about 120 + 150 texts |

The documentation and the website are in English only for now.

## The web page of the bridge

All the texts are in one object, `I18N`, in `open-firenet/web/index.html`, with one block per language:

```js
const I18N = {
  fr: { ... },
  en: { ... },
  de: { ... }
};
```

1. Copy the whole `en: { ... },` block and paste it as a new block named with the two-letter code of your language
   (`it` for Italian, `es` for Spanish...).
2. At the top of your block, set the name of the language in that language, and its flag:
   ```js
   it: {
     langName: "Italiano",
     langFlag: "🇮🇹",
   ```
   That is all it takes for the language to appear in the menu of the page, and to be chosen by itself for people whose
   browser is in that language.
3. Translate the texts: only what is between the quotes on the right of each line. Leave the names on the left as they
   are.
4. Check that nothing is missing:
   ```
   node test/i18n_test.mjs it
   ```
   It lists every text that is missing, empty or misspelt, with its English version.
5. Look at the result in a browser, with example data (no stove needed):
   ```
   python3 tools/gen_screenshots.py --serve
   ```
   then open http://127.0.0.1:8080 and choose your language in the menu. Check the three tabs, and that long words do
   not overflow on a phone-sized window.
6. Regenerate the file the firmware embeds, and commit both files:
   ```
   python3 tools/gen_web_ui.py
   ```

What to leave as it is:

- the names on the left of the colons (`pageTitle`, `mqttSave`...), and the values that are not between quotes
  (`true`, `false`, numbers);
- units and technical names: `°C`, `kg`, `rpm`, `MQTT`, `Wi-Fi`, `MultiAir`, `Open Firenet`, `Home Assistant`, addresses
  such as `http://192.168.4.1`;
- emojis at the beginning of a text;
- `bcpLanguageTag`: put the tag of your language there (`it-IT`), it is used to format numbers and dates.

Keep the texts about as long as the English ones where you can: buttons and tabs have little room, above all on a phone.

Add a line to `CHANGELOG.md` under `## Unreleased`, in English, for example
`- Web page: Italian translation.`

## The Home Assistant integration

In [open-firenet-ha](https://github.com/openfirenet/open-firenet-ha), copy
`custom_components/open_firenet/translations/en.json` to a file named after your language (`it.json`) and translate the
values, on the right of the colons. Home Assistant uses the file by itself for users in that language.

## The installer

In [open-firenet-installer](https://github.com/openfirenet/open-firenet-installer), the texts of the application are in
`src/i18n.js`, one block per language like the web page: copy the `en` block and translate it. The language menu is in
`index.html`.

The progress and error messages of the flashing, download and scan steps, and all the texts of the command line, are in
`src-tauri/src/cli_i18n.rs`: add your language to the `CliLang` list at the top, and the compiler then points at every
text that lacks it. This part can be left for later: those messages are then shown in English in the window.

## Questions

Open an issue or ask in your pull request: a partial translation, or a question about what a text refers to, is
welcome.
