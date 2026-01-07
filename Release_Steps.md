# ESP-IDF Component Release Checklist

Component: **drd_handler**  
Namespace: **lbussy**  
Repository type: **Single-component repository**

This checklist describes the exact steps to update and publish a new version of
an ESP-IDF component to the ESP Component Registry.

---

## 1. Prepare the release

- Ensure your working tree is clean
- All intended changes are committed
- `idf_component.yml` exists at the repository root

---

## 2. Bump the component version

Edit `idf_component.yml` and increment the version number.

```yaml
version: "X.Y.Z"
```

The version **must be new**. Published versions cannot be overwritten.

Commit the change:

```bash
git add idf_component.yml
git commit -m "Release X.Y.Z"
```

---

## 3. Verify build locally

Build an example project or a minimal test project that uses the component:

```bash
idf.py build
```

Fix all errors before continuing.

---

## 6. Publish to the Staging ESP Component Registry

For testing purposes, upload the components to the staging server first.  Login to staging:

```bash
compote registry login --profile "staging" --registry-url "https://components-staging.espressif.com" --default-namespace lbussy
```

This command will open a browser window where you can authenticate with your GitHub account. After logging in, you’ll be redirected to a page displaying your token. Copy and paste it into the terminal.

After logging in, the configuration will be saved under the staging profile. The token will be stored in the configuration file automatically, so you don't have to create it manually. You only need to do this once per environment or session.

Passing the `--default-namespace` option is recommended to avoid specifying the namespace on every upload. By default, your GitHub username will be used as the namespace and you will be given permission to upload components to that namespace.

Upload your component to the staging registry by running the following command:

```bash
compote component upload --profile "staging" --name drd_handler
```

## 5. Verify Staging Publication

- Search for `lbussy/drd_handler` in the [ESP Component Registry Staging UI](https://components-staging.espressif.com).

To use it in your project, add the registry URL in your manifest:

```yml
dependencies:
  <your_default_namespace>/drd_handler:
    version: "@X.Y.Z"
    registry_url: https://components-staging.espressif.com
```

Or via command line:

```bash
idf.py add-dependency lbussy/drd_handler@X.Y.Z --profile staging
```

Test to ensure the publication works properly.

## 6. Publish to the Production ESP Component Registry

To log in to the registry server, use the following command:

```bash
compote registry login --profile "prod" --registry-url "https://components.espressif.com" --default-namespace lbussy
```

This command will open a browser window where you can authenticate with your GitHub account. After logging in, you’ll be redirected to a page displaying your token. Copy and paste it into the terminal.

After logging in, the configuration will be saved under the staging profile. The token will be stored in the configuration file automatically, so you don't have to create it manually. You only need to do this once per environment or session.

Passing the `--default-namespace` option is recommended to avoid specifying the namespace on every upload. By default, your GitHub username will be used as the namespace and you will be given permission to upload components to that namespace.

After successfully logging in, upload with:

```bash
compote component upload --profile "prod" --name drd_handler
```

Notes:

- The upload is **immutable**
- If a mistake is found, you must bump the version and upload again

---

## 6. Verify Prod Publication

- Search for `lbussy/drd_handler` in the ESP Component Registry UI.

To use it in your project, add the registry URL in your manifest:

```yml
dependencies:
  <your_default_namespace>/drd_handler:
    version: "@X.Y.Z"
    registry_url: https://components.espressif.com
```

Or via command line:

```bash
idf.py add-dependency lbussy/drd_handler@X.Y.Z --profile prod
```

Test to ensure the publication works properly.

---

## 7. Tag the release in Git (recommended)

Although not required by the registry, tagging is good practice:

```bash
git tag -a vX.Y.Z -m "Release vX.Y.Z"
git push origin vX.Y.Z
```
