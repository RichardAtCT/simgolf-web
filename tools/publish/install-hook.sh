#!/bin/sh
# Installs a post-commit hook that mirrors each commit to the public repo
# (code only; publish the site with tools/publish/sync-public.sh --site).
# SIMGOLF_NO_SYNC=1 git commit ... skips it once. Output: build/sync-public.log.
cd "$(dirname "$0")/../.."
cat > .git/hooks/post-commit <<'HOOK'
#!/bin/sh
[ -n "$SIMGOLF_NO_SYNC" ] && exit 0
root=$(git rev-parse --show-toplevel)
mkdir -p "$root/build"
( "$root/tools/publish/sync-public.sh" >> "$root/build/sync-public.log" 2>&1 \
    || echo "sync-public FAILED for $(git rev-parse --short HEAD); see build/sync-public.log" >&2 ) &
HOOK
chmod +x .git/hooks/post-commit
echo "post-commit hook installed"
