#!/opt/homebrew/bin/bash
set -euo pipefail

orig_dir="$(pwd)"
trap 'cd "$orig_dir"' EXIT

gitroot="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    printf "%s\n" "Error: Not in a git repo." >&2
    exit 1
}

if [[ -z "$gitroot" || "$gitroot" == "/" ]]; then
    printf "%s\n" "Error: Refusing to clean; invalid git root '$gitroot'." >&2
    exit 1
fi

repo_name="$(basename "$gitroot")"
printf "%s\n" "Cleaning repo: $repo_name"
printf "%s\n" "Git root: $gitroot"

# Remove any dependencies.lock files anywhere in the repo
while IFS= read -r -d '' lock_file; do
    case "$lock_file" in
        "$gitroot"/*)
            printf "%s\n" "Removing: $lock_file"
            rm -f -- "$lock_file"
            ;;
        *)
            printf "%s\n" "Warning: Skipping unexpected path '$lock_file'." >&2
            ;;
    esac
done < <(find "$gitroot" -type f -name 'dependencies.lock' -print0)

# Remove generated sdkconfig files, but preserve sdkconfig.defaults*
while IFS= read -r -d '' sdkconfig_file; do
    case "$sdkconfig_file" in
        "$gitroot"/*)
            printf "%s\n" "Removing: $sdkconfig_file"
            rm -f -- "$sdkconfig_file"
            ;;
        *)
            printf "%s\n" "Warning: Skipping unexpected path '$sdkconfig_file'." >&2
            ;;
    esac
done < <(
    find "$gitroot" -type f \
        -name 'sdkconfig*' \
        ! -name 'sdkconfig.defaults' \
        ! -name 'sdkconfig.defaults.*' \
        -print0
)

# Remove any dist directories anywhere in the repo (follow symlinks)
while IFS= read -r -d '' dist_dir; do
    case "$dist_dir" in
        "$gitroot"/*)
            printf "%s\n" "Removing: $dist_dir"
            rm -rf -- "$dist_dir"
            ;;
        *)
            printf "%s\n" "Warning: Skipping unexpected path '$dist_dir'." >&2
            ;;
    esac
done < <(find -L "$gitroot" -type d -name dist -print0)

# Iterate through local components and remove common generated output
components_dir="$gitroot/components"
if [[ -d "$components_dir" ]]; then
    printf "%s\n" "Scanning components under: $components_dir"

    # -L follows symlinks so "local dev" components are handled.
    while IFS= read -r -d '' component_path; do
        case "$component_path" in
            "$gitroot"/*)
                component_name="$(basename "$component_path")"
                printf "%s\n" "Component: $component_name"

                # Common generated dir inside a component
                if [[ -d "$component_path/build" ]]; then
                    printf "%s\n" "Removing: $component_path/build"
                    rm -rf -- "$component_path/build"
                fi
                ;;
            *)
                printf "%s\n" "Warning: Skipping unexpected path '$component_path'." >&2
                ;;
        esac
    done < <(find -L "$components_dir" -mindepth 1 -maxdepth 1 -type d -print0)
fi

# Remove any build directories under any "examples" or "components" trees
for base in "examples" "components"; do
    base_dir="$gitroot/$base"
    if [[ ! -d "$base_dir" ]]; then
        continue
    fi

    while IFS= read -r -d '' build_dir; do
        case "$build_dir" in
            "$gitroot"/*)
                printf "%s\n" "Removing: $build_dir"
                rm -rf -- "$build_dir"
                ;;
            *)
                printf "%s\n" "Warning: Skipping unexpected path '$build_dir'." >&2
                ;;
        esac
    done < <(find -L "$base_dir" -type d -name build -print0)
done
