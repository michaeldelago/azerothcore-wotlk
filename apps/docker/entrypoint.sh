#!/bin/bash

set -xeuo pipefail
IFS=$'\n\t'

function fix_module_sql_paths() {
    DB_NAME="$1"
    BAD_SQL_FILES="$(find modules/ -iname "*.sql" -type f | grep "sql/$DB_NAME" || echo "")"
    if [[ -n "$BAD_SQL_FILES" ]]; then
        for sqlfile in $BAD_SQL_FILES; do
            filename="$(basename "$sqlfile")"
            module_name="$(<<< "$sqlfile" cut -f2 -d/)"
            sql_custom_module_path="data/sql/custom/db_$DB_NAME/$module_name"
            mkdir -pv "$sql_custom_module_path"
            cp -vn "$sqlfile" "$sql_custom_module_path/$filename"
    done
    fi
}

if [[ "$ACORE_COMPONENT" == "dbimport" ]]; then
    fix_module_sql_paths "auth"
    fix_module_sql_paths "world"
    fix_module_sql_paths "characters"
fi

CONF="/azerothcore/env/dist/etc/$ACORE_COMPONENT.conf"
CONF_DIST="/azerothcore/env/dist/etc/$ACORE_COMPONENT.conf.dist"

if [[ -f "$CONF_DIST" ]]; then
    cp -vn "$CONF_DIST" "$CONF"
else
    touch "$CONF"
fi

echo "Starting $ACORE_COMPONENT..."

exec "$@"
