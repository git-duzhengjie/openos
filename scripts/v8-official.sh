#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEPS_DIR=${OPENOS_DEPS_DIR:-"$ROOT/.openos-deps"}
V8_DIR=${OPENOS_V8_DIR:-"$DEPS_DIR/v8"}
V8_OUT=${OPENOS_V8_OUT:-"$V8_DIR/out/openos-host-jitless"}
PIN_FILE=${OPENOS_V8_PIN:-"$ROOT/ports/chromium-openos/v8.official.pin"}
PRIMARY_REPO=${OPENOS_V8_REPO:-"https://github.com/v8/v8.git"}
MIRROR_REPO=${OPENOS_V8_MIRROR_REPO:-"https://chromium.googlesource.com/v8/v8.git"}
HOST_TOOLS_BIN=${OPENOS_HOST_TOOLS_BIN:-"$DEPS_DIR/host-tools/root/usr/bin"}
DEP_CACHE_DIR=${OPENOS_V8_DEP_CACHE_DIR:-"$DEPS_DIR/v8-dep-cache"}
DEP_SEED_DIR=${OPENOS_V8_DEP_SEED_DIR:-}
DEP_FETCH_TIMEOUT=${OPENOS_V8_DEP_FETCH_TIMEOUT:-300}

log() { printf 'OpenOS official V8 intake: %s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

add_host_tools_to_path() {
    if [ -d "$HOST_TOOLS_BIN" ]; then
        PATH="$HOST_TOOLS_BIN:$PATH"
        export PATH
    fi
}

ensure_dirs() {
    mkdir -p "$DEPS_DIR" "$(dirname "$PIN_FILE")" "$DEP_CACHE_DIR"
}

select_repo() {
    local repo
    for repo in "$PRIMARY_REPO" "$MIRROR_REPO"; do
        if git ls-remote "$repo" HEAD >/dev/null 2>&1; then
            printf '%s\n' "$repo"
            return 0
        fi
    done
    return 1
}

resolve_head() {
    local repo=$1
    git ls-remote "$repo" HEAD | awk 'NR == 1 { print $1 }'
}

write_pin() {
    local repo=$1
    local commit=$2
    local source_kind=${3:-git-shallow-partial-checkout}
    local short=${commit:0:12}
    ensure_dirs
    {
        echo 'official_component=v8'
        echo "repository=$repo"
        echo "commit=$commit"
        echo "commit_short=$short"
        echo "source_kind=$source_kind"
        echo "source_path=$V8_DIR"
        echo "gn_out=$V8_OUT"
        date -u '+generated_at_utc=%Y-%m-%dT%H:%M:%SZ'
        echo 'jitless_smoke=d8 --jitless -e "print(1+2)"'
        echo 'note=Official V8 d8/shell jitless minimum build pin for OpenOS Chromium route.'
    } > "$PIN_FILE"
    cat "$PIN_FILE"
}

check_tools() {
    add_host_tools_to_path
    log "checking prerequisites"
    have git || fail "git is required"
    have python3 || have python || fail "python3/python is required"
    have curl || fail "curl is required"
    have tar || fail "tar is required"
    have gn || fail "gn is required; run ./build.sh host-tools-check or add depot_tools/host-tools to PATH"
    have ninja || fail "ninja is required; run ./build.sh host-tools-check or add depot_tools/host-tools to PATH"
    have clang++ || have g++ || fail "clang++ or g++ is required"
    log "deps_dir:       $DEPS_DIR"
    log "v8_dir:         $V8_DIR"
    log "v8_out:         $V8_OUT"
    log "pin_file:       $PIN_FILE"
    log "host_tools_bin: $HOST_TOOLS_BIN"
    log "dep_cache_dir:  $DEP_CACHE_DIR"
    log "dep_seed_dir:   ${DEP_SEED_DIR:-<unset>}"
    log "dep_timeout:    ${DEP_FETCH_TIMEOUT}s"
    if [ -f "$PIN_FILE" ]; then
        log "existing pin:"
        sed 's/^/  /' "$PIN_FILE"
    else
        log "pin missing; run scripts/v8-official.sh --fetch"
    fi
}

fetch_v8_tarball() {
    local repo=$1
    local commit=$2
    local short=${commit:0:12}
    local tar_dir="$DEPS_DIR/v8-tar"
    local tarball="$DEPS_DIR/v8-$short.tar.gz"
    local archive_url="https://codeload.github.com/v8/v8/tar.gz/$commit"

    have curl || fail "curl is required for GitHub tarball fallback"
    have tar || fail "tar is required for GitHub tarball fallback"

    log "falling back to GitHub tarball $archive_url"
    rm -rf "$tar_dir" "$V8_DIR"
    mkdir -p "$tar_dir"
    curl -L --fail --retry 5 --retry-delay 3 -o "$tarball" "$archive_url"
    tar -xzf "$tarball" -C "$tar_dir" --strip-components=1
    mv "$tar_dir" "$V8_DIR"
    [ -f "$V8_DIR/BUILD.gn" ] || fail "V8 tarball unpack did not produce BUILD.gn"
    write_pin "$repo" "$commit" "github-tarball"
}

fetch_v8() {
    ensure_dirs
    add_host_tools_to_path
    local repo commit
    repo=$(select_repo) || fail "unable to reach official V8 repositories: $PRIMARY_REPO or $MIRROR_REPO"
    commit=$(resolve_head "$repo")
    [ -n "$commit" ] || fail "unable to resolve V8 HEAD from $repo"

    if [ ! -d "$V8_DIR/.git" ]; then
        rm -rf "$V8_DIR"
        mkdir -p "$V8_DIR"
        git -C "$V8_DIR" init
        git -C "$V8_DIR" remote add origin "$repo"
    else
        git -C "$V8_DIR" remote set-url origin "$repo"
    fi

    log "fetching V8 $commit from $repo"
    if git -C "$V8_DIR" -c http.version=HTTP/1.1 fetch --depth 1 --filter=blob:none origin "$commit" && \
       git -C "$V8_DIR" checkout --force --detach FETCH_HEAD; then
        write_pin "$repo" "$commit" "git-shallow-partial-checkout"
    else
        log "git fetch failed; cleaning partial checkout"
        fetch_v8_tarball "$repo" "$commit"
    fi
}

dependency_archive_urls() {
    local repo=$1
    local commit=$2

    case "$repo" in
        external/github.com/*.git)
            local github_path=${repo#external/github.com/}
            github_path=${github_path%.git}
            printf 'https://codeload.github.com/%s/tar.gz/%s\n' "$github_path" "$commit"
            printf 'https://chromium.googlesource.com/%s/+archive/%s.tar.gz\n' "$repo" "$commit"
            ;;
        chromium/src/third_party/simdutf)
            printf 'https://codeload.github.com/simdutf/simdutf/tar.gz/%s\n' "$commit"
            printf 'https://chromium.googlesource.com/%s/+archive/%s.tar.gz\n' "$repo" "$commit"
            ;;
        chromium/src/third_party/abseil-cpp.git)
            printf 'https://codeload.github.com/abseil/abseil-cpp/tar.gz/%s\n' "$commit"
            printf 'https://chromium.googlesource.com/%s/+archive/%s.tar.gz\n' "$repo" "$commit"
            ;;
        chromium/src/third_party/zlib.git)
            printf 'https://codeload.github.com/madler/zlib/tar.gz/%s\n' "$commit"
            printf 'https://chromium.googlesource.com/%s/+archive/%s.tar.gz\n' "$repo" "$commit"
            ;;
        chromium/deps/icu.git)
            printf 'https://codeload.github.com/unicode-org/icu/tar.gz/%s\n' "$commit"
            printf 'https://chromium.googlesource.com/%s/+archive/%s.tar.gz\n' "$repo" "$commit"
            ;;
        *)
            # Chromium's split build/buildtools/tools repositories currently do not
            # have authoritative GitHub mirrors that accept their pinned commits.
            # Keep Gitiles as the official Chromium fallback for those pins.
            printf 'https://chromium.googlesource.com/%s/+archive/%s.tar.gz\n' "$repo" "$commit"
            ;;
    esac
}

copy_dependency_seed() {
    local dest=$1
    local label=$2
    local short=$3
    local rel=${dest#$V8_DIR/}
    local seed
    local marker="$dest/.openos-dep-${label}-${short}.done"

    [ -n "$DEP_SEED_DIR" ] || return 1
    seed="$DEP_SEED_DIR/$rel"
    [ -d "$seed" ] || return 1

    log "copying dependency ${rel} from seed directory $DEP_SEED_DIR"
    rm -rf "$dest"
    mkdir -p "$(dirname "$dest")" "$dest"
    cp -a "$seed"/. "$dest"/
    printf 'seed=%s\ncommit=%s\n' "$seed" "$short" > "$marker"
    return 0
}

download_dependency_archive() {
    local dest=$1
    local repo=$2
    local commit=$3
    local label=$4
    local short=${commit:0:12}
    local cache="$DEP_CACHE_DIR/${label}-${short}.tar.gz"
    local meta="$DEP_CACHE_DIR/${label}-${short}.source"
    local marker="$dest/.openos-dep-${label}-${short}.done"
    local tmp_cache="$cache.tmp.$$"
    local tmp_extract="$dest.tmp-extract.$$"
    local archive_url
    local top_entries
    local first_entry

    if [ -f "$marker" ]; then
        log "dependency already present: ${dest#$V8_DIR/}"
        return 0
    fi

    if [ -d "$dest" ] && [ -n "$(find "$dest" -mindepth 1 -maxdepth 1 2>/dev/null | head -1)" ]; then
        log "removing incomplete dependency directory: ${dest#$V8_DIR/}"
        rm -rf "$dest"
    fi

    if copy_dependency_seed "$dest" "$label" "$short"; then
        return 0
    fi

    mkdir -p "$DEP_CACHE_DIR"
    if [ ! -s "$cache" ]; then
        rm -f "$tmp_cache"
        while IFS= read -r archive_url; do
            [ -n "$archive_url" ] || continue
            log "fetching dependency ${dest#$V8_DIR/} from $archive_url"
            if curl -L --fail --retry 5 --retry-delay 3 --connect-timeout 30 --max-time "$DEP_FETCH_TIMEOUT" -o "$tmp_cache" "$archive_url"; then
                mv "$tmp_cache" "$cache"
                printf '%s\n' "$archive_url" > "$meta"
                break
            fi
            rm -f "$tmp_cache"
            log "dependency source failed, trying next candidate: $archive_url"
        done < <(dependency_archive_urls "$repo" "$commit")
    fi

    [ -s "$cache" ] || fail "failed to download ${dest#$V8_DIR/}; no dependency source candidate worked; set OPENOS_V8_DEP_FETCH_TIMEOUT to increase the timeout, set OPENOS_V8_DEP_SEED_DIR to a reusable Chromium/V8 checkout, or retry later"

    rm -rf "$tmp_extract" "$dest"
    mkdir -p "$tmp_extract" "$dest"
    tar -xzf "$cache" -C "$tmp_extract"

    # GitHub codeload archives contain a single repository-root directory,
    # while Gitiles +archive tarballs contain repository-relative files.
    top_entries=$(find "$tmp_extract" -mindepth 1 -maxdepth 1 | wc -l)
    first_entry=$(find "$tmp_extract" -mindepth 1 -maxdepth 1 | head -1)
    if [ "$top_entries" = "1" ] && [ -d "$first_entry" ]; then
        cp -a "$first_entry"/. "$dest"/
    else
        cp -a "$tmp_extract"/. "$dest"/
    fi
    rm -rf "$tmp_extract"
    printf 'repo=%s\ncommit=%s\n' "$repo" "$commit" > "$marker"
}

sync_minimal_tarball_deps() {
    [ -f "$V8_DIR/DEPS" ] || fail "V8 DEPS missing; run --fetch first"
    ensure_dirs
    have curl || fail "curl is required for dependency tarball sync"
    have tar || fail "tar is required for dependency tarball sync"

    # Commit pins are taken from the checked V8 DEPS file for the pinned source.
    download_dependency_archive "$V8_DIR/build" "chromium/src/build.git" "91dd0147f2b7419d8718fc9811756187e5b0e8af" "build"
    download_dependency_archive "$V8_DIR/buildtools" "chromium/src/buildtools.git" "0d39be5a3f129cf1f35e7812108a2184e2193315" "buildtools"
    download_dependency_archive "$V8_DIR/third_party/dragonbox/src" "external/github.com/jk-jeon/dragonbox.git" "beeeef91cf6fef89a4d4ba5e95d47ca64ccb3a44" "dragonbox"
    download_dependency_archive "$V8_DIR/third_party/fp16/src" "external/github.com/Maratyszcza/FP16.git" "3d2de1816307bac63c16a297e8c4dc501b4076df" "fp16"
    download_dependency_archive "$V8_DIR/third_party/fast_float/src" "external/github.com/fastfloat/fast_float.git" "34164f547b7df3f5d794ff67e9f885c36819ebfc" "fast_float"
    download_dependency_archive "$V8_DIR/third_party/simdutf" "chromium/src/third_party/simdutf" "f7356eed293f8208c40b3c1b344a50bd70971983" "simdutf"
    download_dependency_archive "$V8_DIR/third_party/googletest/src" "external/github.com/google/googletest.git" "4fe3307fb2d9f86d19777c7eb0e4809e9694dde7" "googletest"
    download_dependency_archive "$V8_DIR/third_party/icu" "chromium/deps/icu.git" "d578f2e8b7bd5938e21cfb6bf15c079e0aa5b738" "icu"
    download_dependency_archive "$V8_DIR/third_party/zlib" "chromium/src/third_party/zlib.git" "3246f1b60849cc505e231c5d19d0cbf358093555" "zlib"
    download_dependency_archive "$V8_DIR/third_party/abseil-cpp" "chromium/src/third_party/abseil-cpp.git" "7808b6332fd5a14b7a8f7c76db9d1b78f5fc1117" "abseil-cpp"
    download_dependency_archive "$V8_DIR/tools/clang" "chromium/src/tools/clang.git" "63300441cf30c47dce0f55ebaf2e2a797780f043" "tools-clang"
    download_dependency_archive "$V8_DIR/tools/protoc_wrapper" "chromium/src/tools/protoc_wrapper.git" "418c65786fdf6fc5f10cb008c252c2b12c4713a6" "protoc-wrapper"
}

sync_deps() {
    [ -f "$V8_DIR/BUILD.gn" ] || fail "V8 source missing; run --fetch first"
    add_host_tools_to_path
    if [ -d "$V8_DIR/.git" ] && [ -x "$V8_DIR/tools/dev/v8gen.py" ] && have gclient; then
        log "syncing V8 dependencies with gclient sync"
        (cd "$V8_DIR" && gclient config --unmanaged "$(git remote get-url origin)" && gclient sync --no-history --shallow --with_branch_heads=false --with_tags=false)
    else
        log "gclient or reusable git checkout unavailable; syncing minimal DEPS tarballs"
        sync_minimal_tarball_deps
    fi
}

gn_gen() {
    [ -f "$V8_DIR/BUILD.gn" ] || fail "V8 source missing; run --fetch first"
    add_host_tools_to_path
    mkdir -p "$V8_OUT"
    log "generating GN files for host d8 jitless smoke"
    (cd "$V8_DIR" && gn gen "$V8_OUT" --args='is_debug=false is_component_build=false target_os="linux" target_cpu="x64" use_custom_libcxx=false v8_enable_i18n_support=false v8_use_external_startup_data=false v8_enable_pointer_compression=false v8_enable_sandbox=false v8_enable_webassembly=false treat_warnings_as_errors=false')
}

build_d8() {
    sync_deps
    gn_gen
    add_host_tools_to_path
    log "building d8"
    ninja -C "$V8_OUT" d8
}

smoke() {
    local d8="$V8_OUT/d8"
    [ -x "$d8" ] || fail "d8 missing; run --build first"
    log "running jitless d8 smoke"
    local output
    output=$("$d8" --jitless -e 'print(1 + 2)')
    [ "$output" = "3" ] || fail "unexpected d8 smoke output: $output"
    log "d8 jitless smoke passed"
}

usage() {
    cat <<USAGE
Usage: scripts/v8-official.sh [--check|--fetch|--sync-deps|--gn-gen|--build|--smoke|--all]

Environment:
  OPENOS_V8_REPO          Primary official V8 git URL (default: $PRIMARY_REPO)
  OPENOS_V8_MIRROR_REPO   Mirror fallback URL (default: $MIRROR_REPO)
  OPENOS_HOST_TOOLS_BIN   Directory containing gn/ninja (default: $HOST_TOOLS_BIN)
  OPENOS_V8_DIR           Checkout directory (default: $V8_DIR)
  OPENOS_V8_OUT           GN output directory (default: $V8_OUT)
  OPENOS_V8_DEP_SEED_DIR      Optional reusable Chromium/V8 checkout used before network dependency downloads
  OPENOS_V8_DEP_FETCH_TIMEOUT Dependency archive download timeout in seconds (default: $DEP_FETCH_TIMEOUT)
USAGE
}

case "${1:---check}" in
    --check|check)
        check_tools
        ;;
    --fetch|fetch)
        fetch_v8
        ;;
    --sync-deps|sync-deps)
        sync_deps
        ;;
    --gn-gen|gn-gen)
        gn_gen
        ;;
    --build|build)
        build_d8
        ;;
    --smoke|smoke)
        smoke
        ;;
    --all|all)
        check_tools
        fetch_v8
        sync_deps
        build_d8
        smoke
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
