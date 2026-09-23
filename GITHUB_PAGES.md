# Publishing SolarProxxie on GitHub

The repository target is:

```text
https://github.com/martinville/SolarProxxie
```

The expected GitHub Pages address is:

```text
https://martinville.github.io/SolarProxxie/
```

## What to upload

Upload the **entire contents of the ESPGhostNode project root** into the root of the
`SolarProxxie` repository. Do not place the whole project inside another
`SolarProxxie` subfolder.

The important layout is:

```text
SolarProxxie/
├── .github/workflows/pages.yml
├── .github/workflows/release.yml
├── main/
├── site/
│   ├── index.html
│   ├── style.css
│   ├── app.js
│   ├── logo.svg
│   └── firmware/
├── tools/
├── CMakeLists.txt
├── VERSION
└── README.md
```

`site/` is the GitHub Pages website source. The Pages workflow builds the firmware,
creates the browser-installable full image, and publishes `site/` as the website.
The source code under `main/` and the build files at the repository root are needed
because the workflow rebuilds firmware instead of trusting an old checked-in binary.

Do not upload `.tools/`, `.venv/`, `node_modules/`, or local packet captures merely
to make Pages work. Those are development or private/local files and are not needed
by the hosted website.

## Enable GitHub Pages

1. Push the project to the repository's `main` branch.
2. Open **Repository Settings → Pages**.
3. Under **Build and deployment**, set **Source** to **GitHub Actions**.
4. Open the **Actions** tab and allow the `Build firmware and deploy GitHub Pages`
   workflow to complete.
5. Open `https://martinville.github.io/SolarProxxie/`.

GitHub Pages serves the site over HTTPS, which is required for browser serial access.
The browser installer itself is supplied by ESP Web Tools and currently targets a
desktop Chromium browser such as Chrome or Edge.

## Publishing cloud updates for installed devices

The Firmware page on the ESP32 checks the repository's latest **GitHub Release**. It
does not install arbitrary files from the `main` branch.

To publish version `1.2.0`:

1. Update `VERSION` to `1.2.0` and update `CHANGELOG.md`.
2. Commit and push the tested source to `main`.
3. Create and push the matching tag:

   ```sh
   git tag v1.2.0
   git push origin v1.2.0
   ```

4. The `Publish firmware release` workflow builds the application and creates a
   GitHub Release containing `SolarProxxie.bin`, `SolarProxxie-full.bin`, and
   `release.json`.

The ESP32 accepts only a release asset named `SolarProxxie.bin` from the fixed
`martinville/SolarProxxie` release path. It checks that the downloaded image embeds
the expected SolarProxxie project name and release version before selecting it for
boot.

## Repository access

Do not paste a GitHub password or personal access token into chat. To let an automated
tool publish, use either a GitHub connector authorized for the repository or sign in
locally with GitHub CLI using `gh auth login`. The account needs permission to create
or push to `martinville/SolarProxxie`, configure Pages, and run Actions.
