# Contributing to Open Firenet

Thank you for your interest! You do not need to write code to help: reports and tests from people whose stove we do
not have are what moved the project forward most. The website has the overview of every way to help:
**https://openfirenet.github.io/contribute.html**

In short:

- **Your stove works, or does not?** Open an issue with the matching form ("It works on my stove" or "Problem with the
  bridge or the stove link"). The forms ask for what we need: stove model and firmware version, bridge version, board,
  log, `/api/state`.
- **Translations**: see [docs/TRANSLATING.md](docs/TRANSLATING.md). No programming needed.
- **Documentation**: the website is in [openfirenet.github.io](https://github.com/openfirenet/openfirenet.github.io).
- **Home Assistant integration** and **installer**: they have their own repositories,
  [open-firenet-ha](https://github.com/openfirenet/open-firenet-ha) and
  [open-firenet-installer](https://github.com/openfirenet/open-firenet-installer).

## Changing the firmware

1. **Open an issue first**, or comment on an existing one, to agree on what to do.
2. **One branch per change**, named after the issue: `fix/42-short-description`, `feat/42-short-description`.
3. **Build and test.** `./test/build_and_test.sh` runs every test on your computer, without hardware: the protocol
   and the link with a simulated stove, the API command parsing, the API description (`openapi.yaml`) and the
   translations. The README explains how to compile and flash the firmware (`flash.sh`).
4. **Keep the generated and descriptive files in step.** The tests tell you when one is forgotten:
   - after editing `open-firenet/web/index.html`, run `python3 tools/gen_web_ui.py` and commit both files;
   - a change of the REST API goes with its description in `openapi.yaml`;
   - a new text of the web page is added to every language of its `I18N` object.
5. **Add a line to `CHANGELOG.md`** under `## Unreleased`, in English, written for users: what changes for them.
6. **Open a pull request** whose description starts with `Fixes #42` (or `Refs #42` for a partial change).

Commit messages start with `feat:`, `fix:`, `docs:` or `chore:`, on one line: the release script reads them to choose
the next version number. Code comments are in English.

## Changes to the dialogue with the stove

The protocol was reverse-engineered ([PROTOCOL.md](PROTOCOL.md)), and a wrong message can make a stove refuse the
bridge. A change in what is sent to the stove needs a test on a real stove before it is merged, ideally on each
firmware family (2.29, 2.28, 2.26 / 2.27). Say in your pull request what you tested, on which stove, and what you
could not test: we will look for testers.

## License

By contributing you agree that your contribution is distributed under the license of the project, the
[GNU AGPL v3.0 or later](LICENSE).
