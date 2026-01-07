# ESP-IDF Component Release Checklist

Component: **drd_handler**  
Namespace: **lbussy**  
Repository type: **Single-component repository**

This checklist describes the exact steps to update and publish a new version of
an ESP-IDF component to the ESP Component Registry.

---

## Update Instructions

- Search and replace `1.0.1` and replace with the new version
- Search and replace `drd_handler` and replace with the component name (if needed)
- Search and replace `lbussy` and replace with the correct namespace (if needed)

---

## 1. Verify Build Locally

Build the example project or a minimal test project that uses the component.

```bash
cd examples/basic
```

Edit `examples/basic/main/idf_component.yml` and comment out the component line to use the local `components` directory (link to GitHub root):

```yml
dependencies:
  #lbussy/drd_handler: "^1.0.1"
  idf: ">=5.5"
```

Set environment:

```bash
export DRD_HANDLER_LOCAL_DEV=1
```

```bash
idf.py build
```

Test thoroughly, continue when ready.

Edit `examples/basic/main/idf_component.yml` and uncomment out the component line to use the component registry:

```yml
dependencies:
  lbussy/drd_handler: "^1.0.1"
  idf: ">=5.5"
```

Unset environment:

```bash
export DRD_HANDLER_LOCAL_DEV=
```

## 2. Prepare the Release

- Ensure your working tree is clean
- All intended changes are committed
- `idf_component.yml` exists at the repository root

## 3. Bump Component Version

Edit `idf_component.yml` and increment the version number.

```yaml
version: "1.0.1"
```

The version **must be new**. Published versions cannot be overwritten.

Commit the change at the repository root:

```bash
git add idf_component.yml # and any other updated files
git commit -m "Release 1.0.1"
git tag -a 1.0.1 -m "Release v1.0.1"
git push
git push origin 1.0.1
```

## 4. Publish to the Staging ESP Component Registry

For testing purposes, upload the components to the staging server first.

If you have not logged in to staging previously, login to staging:

```bash
compote registry login --profile "staging" --registry-url "https://components-staging.espressif.com" --default-namespace lbussy
```

This command will open a browser window where you can authenticate with your GitHub account. After logging in, you’ll be redirected to a page displaying your token. Copy and paste it into the terminal.

After logging in, the configuration will be saved under the staging profile and the token will be stored in the configuration file automatically. You only need to do this once per environment or session.

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
  lbussy/drd_handler:
    version: "^1.0.1"
    registry_url: https://components-staging.espressif.com
```

Or via command line:

```bash
idf.py add-dependency lbussy/drd_handler>=1.0.1 --profile staging
```

Test to ensure the publication works properly.

## 6. Publish to the Production ESP Component Registry

If you have not logged in to production previously, login to production:

```bash
compote registry login --profile "prod" --registry-url "https://components.espressif.com" --default-namespace lbussy
```

This command will open a browser window where you can authenticate with your GitHub account. After logging in, you’ll be redirected to a page displaying your token. Copy and paste it into the terminal.

After logging in, the configuration will be saved under the production profile and the token will be stored in the configuration file automatically. You only need to do this once per environment or session.

Passing the `--default-namespace` option is recommended to avoid specifying the namespace on every upload. By default, your GitHub username will be used as the namespace and you will be given permission to upload components to that namespace.

Upload your component to the production registry by running the following command:

```bash
compote component upload --profile "prod" --name drd_handler
```

Notes:

- The upload is **immutable**
- If a mistake is found, you must bump the version and upload again

---

## 7. Verify Prod Publication

- Search for `lbussy/drd_handler` in the [ESP Component Registry Production UI](https://components.espressif.com).

To use it in your project, add the registry URL in your manifest:

```yml
dependencies:
  lbussy/drd_handler:
    version: "^1.0.1"
    registry_url: https://components.espressif.com
```

Or via command line:

```bash
idf.py add-dependency lbussy/drd_handler>=1.0.1 --profile prod
```

Test to ensure the publication works properly.
