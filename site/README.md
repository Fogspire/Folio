# Docs site

Builds the GitHub Pages site from the repository's markdown. Every page is generated; edit the `.md` files, not the site.

## Preview locally

```bash
cd site
npm ci
node build.mjs --serve
```

Open http://localhost:4173/. On Windows, `preview.bat` does all of this and opens the browser.

## Publish

`.github/workflows/pages.yml` builds and deploys on every push to `main` that touches markdown or `site/`. Once per repository, set **Settings > Pages > Source** to **GitHub Actions**.

## Troubleshooting

- **A page is missing from the sidebar.** The sidebar is read from the tables in `docs/README.md`. A page not linked there is listed under **More** and the build prints its name.
- **A link goes to GitHub instead of a page.** Links to anything that is not a published `.md` file or a file under `docs/` point at the source on GitHub.
