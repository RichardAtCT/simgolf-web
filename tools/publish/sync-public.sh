#!/bin/sh
# Mirrors this private dev repo to the public repo (RichardAtCT/simgolf-web).
#
#   tools/publish/sync-public.sh          code: the committed tree at HEAD, minus
#                                         .publicignore, onto the public main
#   tools/publish/sync-public.sh --site   also rebuild the Pages site
#                                         (tools/pages/build.sh) and push gh-pages
#
# The public main gets one commit per sync, listing the private commits it
# covers; its "Private-Commit:" trailer records where the next sync starts.
# Refuses to push if the tree holds anything that looks like game data,
# decompiler output or a personal path. tools/publish/install-hook.sh runs the
# code sync after every commit.
set -e
cd "$(dirname "$0")/../.."
root=$(pwd)
REPO=${PUBLIC_REPO:-https://github.com/RichardAtCT/simgolf-web.git}
work=$root/build/public
site=$root/build/public-site
author_name=$(git config user.name || echo RichardAtCT)
author_email=$(git config user.email || echo 29794543+RichardAtCT@users.noreply.github.com)

head=$(git rev-parse HEAD)
short=$(git rev-parse --short HEAD)

# ---- public checkout ----
if [ ! -d "$work/.git" ]; then
  rm -rf "$work"; git clone -q --branch main "$REPO" "$work"
else
  git -C "$work" fetch -q origin main && git -C "$work" reset -q --hard origin/main
fi
last=$(git -C "$work" log -1 --format=%B | sed -n 's/^Private-Commit: //p' | tail -1)

# ---- export ----
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git archive HEAD | tar -x -C "$tmp"
grep -v '^#' .publicignore | grep -v '^$' | while read -r p; do rm -rf "$tmp/$p"; done

# ---- safety checks ----
bad=$(cd "$tmp" && find . -type f \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.bik' -o -iname '*.flc' \
  -o -iname '*.pcx' -o -iname '*.wav' -o -iname '*.sve' -o -iname '*.iso' -o -iname '*.bin' -o -iname '*.cab' \
  -o -iname '*.hdr' -o -iname '*.gpr' -o -path './build/*' -o -path './game/*' -o -path './assets/*' \
  -o -path './decomp/*' -o -iname 'functions*.jsonl' -o -iname 'image.bin' \))
big=$(cd "$tmp" && find . -type f -size +2048k ! -path './third_party/*')
personal=$(cd "$tmp" && grep -rIl -e '/Use''rs/' -e 'Richards''-Mac' -e '\.lo''cal:' . || true)   # split so this file doesn't match itself
if [ -n "$bad$big$personal" ]; then
  echo "sync-public: refusing to publish:" >&2
  [ -n "$bad" ] && echo "  game or build files: $bad" >&2
  [ -n "$big" ] && echo "  files over 2 MB: $big" >&2
  [ -n "$personal" ] && echo "  personal paths in: $personal" >&2
  exit 1
fi

# ---- commit ----
rsync -a --delete --exclude .git "$tmp/" "$work/"
if [ -z "$(git -C "$work" status --porcelain)" ]; then
  echo "sync-public: public main already matches $short"
else
  if [ -n "$last" ]; then range="$last..HEAD"; else range="-1 HEAD"; fi
  subjects=$(git log --reverse --format='- %s' $range 2>/dev/null | grep -v -i -e 'HANDOFF' || true)
  msg="Sync from development ($short)

$subjects

Private-Commit: $head"
  git -C "$work" add -A
  git -C "$work" -c user.name="$author_name" -c user.email="$author_email" commit -q -m "$msg"
  git -C "$work" push -q origin main
  echo "sync-public: pushed $(git -C "$work" rev-parse --short HEAD) to public main"
fi

# ---- site ----
if [ "$1" = "--site" ]; then
  tools/pages/build.sh >/dev/null
  if [ ! -d "$site/.git" ]; then
    rm -rf "$site"; git clone -q --branch gh-pages "$REPO" "$site"
  else
    git -C "$site" fetch -q origin gh-pages && git -C "$site" reset -q --hard origin/gh-pages
  fi
  rsync -a --delete --exclude .git build/pages/site/ "$site/"
  if [ -n "$(git -C "$site" status --porcelain)" ]; then
    git -C "$site" add -A
    git -C "$site" -c user.name="$author_name" -c user.email="$author_email" commit -q -m "Pages build $short"
    git -C "$site" push -q origin gh-pages
    echo "sync-public: pushed the site build ($short) to gh-pages"
  else
    echo "sync-public: site unchanged"
  fi
fi
