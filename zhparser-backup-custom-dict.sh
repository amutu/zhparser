#!/usr/bin/env bash
#
# zhparser-backup-custom-dict.sh
#
# Manage zhparser custom dict files (zhprs_dict_*) under $PGDATA/base.
#
# Usage:
#   backup  <pgdata_dir>                       Copy zhprs_dict_* to a timestamped backup dir.
#   restore <pgdata_dir> <restore_from_dir>    Copy zhprs_dict_* from <restore_from_dir> back to <pgdata_dir>/base.
#   delete  <pgdata_dir> [--yes] [--dry-run]   Delete zhprs_dict_* under <pgdata_dir>/base.
#
# Hardening (vs. original):
#   - set -euo pipefail
#   - Validates PGDATA exists and looks plausible.
#   - delete refuses to run without --yes; supports --dry-run.
#   - Uses null-delimited globs to handle weird filenames.

set -euo pipefail

usage() {
    cat <<EOF >&2
usage:
  $0 backup  <pgdata_dir>
  $0 restore <pgdata_dir> <restore_from_dir>
  $0 delete  <pgdata_dir> [--yes] [--dry-run]

WARNING: 'delete' permanently removes zhparser custom dict files.
         Always run 'backup' first.
EOF
    exit 2
}

require_pgdata() {
    local d="$1"
    if [ ! -d "$d" ] || [ ! -d "$d/base" ]; then
        echo "error: \"$d\" does not look like a PGDATA directory (missing $d/base)" >&2
        exit 1
    fi
}

cmd="${1:-}"
[ -n "$cmd" ] || usage

case "$cmd" in
    backup)
        [ "$#" -ge 2 ] || usage
        pgdata="$2"
        require_pgdata "$pgdata"
        ts=$(date +'%Y-%m-%dT%H-%M-%S')
        backup_dir="zhparser-backup-custom-dict-$ts"
        mkdir "$backup_dir"
        echo "Backing up $pgdata/base/zhprs_dict_* -> $backup_dir/"
        # Avoid noisy 'no match' if there are zero matching files.
        shopt -s nullglob
        files=( "$pgdata"/base/zhprs_dict_* )
        shopt -u nullglob
        if [ "${#files[@]}" -eq 0 ]; then
            echo "no zhprs_dict_* files found; backup directory left empty"
        else
            cp -a -- "${files[@]}" "$backup_dir/"
            echo "backup ok"
        fi
        ;;

    restore)
        [ "$#" -ge 3 ] || usage
        pgdata="$2"
        restore_from_dir="$3"
        require_pgdata "$pgdata"
        if [ ! -d "$restore_from_dir" ]; then
            echo "error: restore source \"$restore_from_dir\" does not exist" >&2
            exit 1
        fi
        echo "Restoring $restore_from_dir/zhprs_dict_* -> $pgdata/base/"
        shopt -s nullglob
        files=( "$restore_from_dir"/zhprs_dict_* )
        shopt -u nullglob
        if [ "${#files[@]}" -eq 0 ]; then
            echo "error: no zhprs_dict_* files in $restore_from_dir" >&2
            exit 1
        fi
        cp -a -- "${files[@]}" "$pgdata/base/"
        echo "restore ok"
        ;;

    delete)
        [ "$#" -ge 2 ] || usage
        pgdata="$2"
        require_pgdata "$pgdata"
        shift 2
        confirm="no"
        dry_run="no"
        while [ "$#" -gt 0 ]; do
            case "$1" in
                --yes)     confirm="yes" ;;
                --dry-run) dry_run="yes" ;;
                *) echo "unknown flag: $1" >&2; exit 2 ;;
            esac
            shift
        done

        shopt -s nullglob
        files=( "$pgdata"/base/zhprs_dict_* )
        shopt -u nullglob

        if [ "${#files[@]}" -eq 0 ]; then
            echo "nothing to delete: no zhprs_dict_* under $pgdata/base"
            exit 0
        fi

        echo "Will delete the following files under $pgdata/base:"
        for f in "${files[@]}"; do
            echo "  $f"
        done

        if [ "$dry_run" = "yes" ]; then
            echo "(dry-run, no files removed)"
            exit 0
        fi

        if [ "$confirm" != "yes" ]; then
            echo
            echo "REFUSING to delete without --yes. Re-run with:"
            echo "    $0 delete $pgdata --yes"
            exit 1
        fi

        rm -- "${files[@]}"
        echo "delete ok"
        ;;

    *)
        usage
        ;;
esac
