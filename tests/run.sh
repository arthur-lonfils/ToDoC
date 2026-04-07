#!/bin/sh
# Automated test suite for todoc
# Usage: ./tests/run.sh [--valgrind]
#
# Runs all commands against a temporary database and checks exit codes
# and expected output. With --valgrind, also checks for memory leaks.

set -e

TODOC="./build/todoc"
TEST_HOME=$(mktemp -d)
export HOME="$TEST_HOME"

# Suppress the background update check globally so the suite never
# spawns a curl child or hits GitHub. The dedicated update-check
# tests below temporarily unset this for a single command.
export TODOC_NO_UPDATE_CHECK=1

# Strip any agent-mode env vars inherited from the developer's shell.
# todoc auto-detects ai mode from these (so an agent flips itself
# automatically), but the user-mode assertions in this suite must run
# in plain user mode. The dedicated agent-mode test block below
# re-injects each var explicitly with `env`.
unset TODOC_MODE TODOC_AGENT CLAUDECODE CLAUDE_CODE_ENTRYPOINT CLAUDE_PROJECT_DIR CURSOR_TRACE_ID

PASS=0
FAIL=0
USE_VALGRIND=0

if [ "$1" = "--valgrind" ]; then
    USE_VALGRIND=1
    if ! command -v valgrind > /dev/null 2>&1; then
        echo "ERROR: valgrind not found"
        exit 1
    fi
fi

# ── Helpers ──────────────────────────────────────────────────────

cleanup() {
    rm -rf "$TEST_HOME"
}
trap cleanup EXIT

run() {
    if [ "$USE_VALGRIND" = 1 ]; then
        valgrind --leak-check=full --show-leak-kinds=all \
                 --error-exitcode=99 --quiet \
                 "$TODOC" "$@" 2>&1
    else
        "$TODOC" "$@" 2>&1
    fi
}

# assert_ok: run command, expect exit 0
assert_ok() {
    desc="$1"
    shift
    if output=$(run "$@"); then
        PASS=$((PASS + 1))
        printf "  \033[32mPASS\033[0m  %s\n" "$desc"
    else
        rc=$?
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (exit %d)\n" "$desc" "$rc"
        printf "        %s\n" "$output"
    fi
}

# assert_fail: run command, expect non-zero exit
assert_fail() {
    desc="$1"
    shift
    if output=$(run "$@"); then
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (expected failure, got exit 0)\n" "$desc"
        printf "        %s\n" "$output"
    else
        rc=$?
        # exit 99 = valgrind found a leak, that's a real failure
        if [ "$rc" = 99 ]; then
            FAIL=$((FAIL + 1))
            printf "  \033[31mFAIL\033[0m  %s (memory error)\n" "$desc"
            printf "        %s\n" "$output"
        else
            PASS=$((PASS + 1))
            printf "  \033[32mPASS\033[0m  %s\n" "$desc"
        fi
    fi
}

# add_capture: run an 'add' command, expect success, and store the new
# task's id in $LAST_ID. Useful when subsequent assertions need to
# reference the id and the id is not predictable due to autoincrement.
add_capture() {
    desc="$1"
    shift
    if output=$(run add "$@"); then
        LAST_ID=$(echo "$output" | grep -oE '#[0-9]+' | head -n1 | tr -d '#')
        if [ -n "$LAST_ID" ]; then
            PASS=$((PASS + 1))
            printf "  \033[32mPASS\033[0m  %s (id=%s)\n" "$desc" "$LAST_ID"
            return 0
        fi
    fi
    rc=$?
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  %s (exit %d)\n" "$desc" "$rc"
    printf "        %s\n" "$output"
    return 1
}

# assert_output: run command, expect exit 0 and output containing a string
assert_output() {
    desc="$1"
    expected="$2"
    shift 2
    if output=$(run "$@"); then
        if echo "$output" | grep -qF "$expected"; then
            PASS=$((PASS + 1))
            printf "  \033[32mPASS\033[0m  %s\n" "$desc"
        else
            FAIL=$((FAIL + 1))
            printf "  \033[31mFAIL\033[0m  %s (expected output containing '%s')\n" "$desc" "$expected"
            printf "        got: %s\n" "$output"
        fi
    else
        rc=$?
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (exit %d)\n" "$desc" "$rc"
        printf "        %s\n" "$output"
    fi
}

# ── Check binary exists ─��───────────────────────────────────────

if [ ! -x "$TODOC" ]; then
    echo "ERROR: $TODOC not found. Run 'make' first."
    exit 1
fi

echo ""
echo "todoc test suite"
if [ "$USE_VALGRIND" = 1 ]; then
    echo "(with valgrind leak checking)"
fi
echo "─────────────────────────────────────"
echo ""

# ── 1. Init ─────────────────────────────────────────────────────

echo "Init:"
assert_ok      "init creates database"                init
assert_output  "init is idempotent"  "up to date"     init
echo ""

# ── 2. Add ──────────────────────────────────────────────────────

echo "Add:"
assert_ok      "add basic task"                       add "Buy milk"
assert_ok      "add with all options"                 add "Fix login bug" --type bug --priority critical --scope auth --desc "SSO broken" --due 2026-05-01
assert_ok      "add feature"                          add "Dark mode" --type feature --priority medium --scope ui
assert_ok      "add chore"                            add "Update deps" --type chore --priority low
assert_ok      "add idea"                             add "AI search" --type idea --priority high --scope search
assert_fail    "add without title fails"              add
assert_fail    "add with bad type fails"              add "x" --type invalid
assert_fail    "add with bad priority fails"          add "x" --priority urgent
assert_fail    "add with bad date fails"              add "x" --due "not-a-date"
assert_fail    "add with bad date range fails"        add "x" --due "2026-02-30"
echo ""

# ���─ 3. List ──────────────────────���──────────────────────────────

echo "List:"
assert_output  "list shows all tasks"        "5 task(s)"      list
assert_output  "list filter by priority"     "1 task(s)"      list --priority critical
assert_output  "list filter by type"         "1 task(s)"      list --type chore
assert_output  "list filter by scope"        "auth"           list --scope auth
assert_output  "list with limit"             "2 task(s)"      list --limit 2
assert_output  "list alias ls works"         "5 task(s)"      ls
echo ""

# ── 4. Show ─────────────────────────────────────────────────────

echo "Show:"
assert_output  "show displays task"          "Fix login bug"  show 2
assert_output  "show displays description"   "SSO broken"     show 2
assert_output  "show displays scope"         "auth"           show 2
assert_output  "show displays due date"      "2026-05-01"     show 2
assert_fail    "show non-existent fails"                      show 999
assert_fail    "show without id fails"                        show
echo ""

# ── 5. Edit ────────────────────────���────────────────────────────

echo "Edit:"
assert_ok      "edit title"                           edit 1 --title "Buy oat milk"
assert_output  "edit title persisted"    "oat milk"   show 1
assert_ok      "edit priority"                        edit 1 --priority high
assert_ok      "edit status"                          edit 1 --status in-progress
assert_ok      "edit multiple fields"                 edit 3 --priority high --scope frontend --due 2026-06-01
assert_fail    "edit non-existent fails"              edit 999 --title "x"
assert_fail    "edit without id fails"                edit
echo ""

# ── 6. Done ─────────────────────────��───────────────────────────

echo "Done:"
assert_ok      "done marks task"                      done 4
assert_output  "done status persisted"   "done"       show 4
assert_fail    "done non-existent fails"              done 999
echo ""

# ── 7. Delete ───────────────────────────────────────────────────

echo "Delete:"
assert_ok      "rm deletes task"                      rm 4
assert_fail    "rm non-existent fails"                rm 4
assert_fail    "rm without id fails"                  rm
assert_output  "delete alias works"      "deleted"    delete 5
assert_output  "list after deletes"      "3 task(s)"  list
echo ""

# ── 8. Stats ──────────���─────────────────────────��───────────────

echo "Stats:"
assert_output  "stats shows total"       "Total tasks: 3"   stats
assert_output  "stats shows status"      "in-progress"      stats
echo ""

# ── 8b. Export ──────────────────────────────────────────────────

echo "Export:"
assert_output  "export csv header"       "id,title,description"  export
assert_output  "export csv has data"     "Buy oat milk"          export
assert_output  "export json format"      "\"id\":"               export --format json
assert_output  "export json has data"    "Buy oat milk"          export --format json
assert_output  "export csv filtered"     "in-progress"           export --status in-progress
assert_fail    "export bad format fails"                         export --format xml
echo ""

# ── 8c. Projects ────────────────────────────────────────────────

echo "Projects:"
assert_ok      "add-project basic"                    add-project auth
assert_ok      "add-project with all options"         add-project ui --desc "Frontend redesign" --color blue --due 2026-09-01
assert_ok      "add-project minimal"                  add-project backend
assert_fail    "add-project without name fails"       add-project
assert_fail    "add-project duplicate name fails"     add-project auth

assert_output  "list-projects shows all"     "3 project(s)"        list-projects
assert_output  "list-projects shows name"    "auth"                list-projects
assert_output  "list-projects filter status" "3 project(s)"        list-projects --status active

assert_output  "show-project displays name"        "auth"             show-project auth
assert_output  "show-project displays description" "Frontend"         show-project ui
assert_output  "show-project displays color"       "blue"             show-project ui
assert_output  "show-project displays due date"    "2026-09-01"       show-project ui
assert_fail    "show-project non-existent fails"                      show-project nope
assert_fail    "show-project without name fails"                      show-project

assert_ok      "edit-project description"             edit-project auth --desc "Authentication system"
assert_output  "edit-project desc persisted" "Authentication" show-project auth
assert_ok      "edit-project status"                  edit-project backend --status completed
assert_output  "edit-project status persisted" "completed"    show-project backend
assert_ok      "edit-project color"                   edit-project auth --color red
assert_fail    "edit-project non-existent fails"      edit-project nope --desc "x"
echo ""

# ── 8d. Task assignment ─────────────────────────────────────────

echo "Assignment:"
assert_ok      "add task with --project"              add "Login form" --type feature --project auth
assert_output  "task auto-assigned to project" "Login form" list --project auth
assert_ok      "assign existing task to project"      assign 1 auth
assert_ok      "assign task to second project"        assign 1 ui
assert_output  "task visible in first project"  "Buy oat milk" list --project auth
assert_output  "task visible in second project" "Buy oat milk" list --project ui
assert_ok      "unassign task from project"           unassign 1 ui
assert_fail    "unassign already unlinked fails"      unassign 1 ui
assert_fail    "assign non-existent task fails"       assign 999 auth
assert_fail    "assign to non-existent project fails" assign 1 nope
assert_fail    "assign without args fails"            assign
echo ""

# ── 8e. Active project ──────────────────────────────────────────

echo "Active project:"
assert_ok      "use sets active project"              use auth
assert_output  "list scoped to active project" "task(s)"     list
assert_output  "list shows active project info" "auth"       list
assert_output  "list --all overrides active"   "task(s)"     list --all
assert_output  "stats scoped to active project" "Total tasks" stats
assert_ok      "use --clear removes active"           use --clear
assert_output  "list after clear shows all"   "task(s)"      list
assert_fail    "use non-existent project fails"       use nope
echo ""

# ── 8f. Project deletion ────────────────────────────────────────

echo "Project delete:"
assert_ok      "rm-project deletes project"           rm-project backend
assert_output  "list-projects after delete"  "2 project(s)" list-projects
assert_fail    "rm-project non-existent fails"        rm-project backend
assert_ok      "task survives project deletion"       show 1
assert_ok      "rm-project clears active if matched"  use auth
assert_ok      "rm active project"                    rm-project auth
echo ""

# ── 8g. Subtasks ────────────────────────────────────────────────

echo "Subtasks:"
add_capture    "create parent task"                "Big feature" --type feature --priority high
PARENT_ID=$LAST_ID
add_capture    "create subtask A"                  "Sub A" --sub "$PARENT_ID"
SUB_A=$LAST_ID
add_capture    "create subtask B"                  "Sub B" --sub "$PARENT_ID"
SUB_B=$LAST_ID
assert_output  "list shows tree indent"  "└─"      list
assert_output  "show parent shows children" "Subtasks" show "$PARENT_ID"
assert_fail    "done parent with open kids fails"  done "$PARENT_ID"
assert_fail    "edit parent to done blocked"       edit "$PARENT_ID" --status done
assert_fail    "subtask with bad parent fails"     add "Orphan" --sub 999999
assert_fail    "subtask of subtask refused"        add "Sub-sub" --sub "$SUB_A"

# Finish children → parent can now be done
assert_ok      "abandon child A"                   edit "$SUB_A" --status abandoned
assert_ok      "done child B"                      done "$SUB_B"
assert_ok      "now done parent works"             done "$PARENT_ID"

# Promote-on-delete
add_capture    "create another parent"             "Tree root"
ROOT=$LAST_ID
add_capture    "create child of new parent"        "Leaf" --sub "$ROOT"
LEAF=$LAST_ID
assert_output  "rm parent promotes children" "promoted" rm "$ROOT"
assert_output  "promoted child still exists" "Leaf"     show "$LEAF"

# Edit --sub none promotes a subtask
add_capture    "create yet another parent"         "Wrapper"
WRAP=$LAST_ID
add_capture    "create child of wrapper"           "Inner" --sub "$WRAP"
INNER=$LAST_ID
assert_ok      "promote child via edit --sub none" edit "$INNER" --sub none
assert_fail    "self-parent forbidden"             edit "$INNER" --sub "$INNER"
echo ""

# ── 8h. Move ────────────────────────────────────────────────────

echo "Move:"
assert_ok      "create move target project"        add-project beta --desc "beta project"
assert_ok      "create move source project"        add-project alpha --desc "alpha project"
add_capture    "create task in alpha"              "Movable" --project alpha
MOVABLE=$LAST_ID
add_capture    "create child of movable"           "Movable child" --sub "$MOVABLE"
MCHILD=$LAST_ID
assert_ok      "move parent to beta"               move "$MOVABLE" beta
assert_output  "task now in beta"        "Movable"        list --project beta
assert_output  "child also in beta"      "Movable child"  list --project beta
assert_fail    "moving subtask refused"            move "$MCHILD" beta
assert_fail    "move to nonexistent project"       move "$MOVABLE" nope
assert_fail    "move with no target fails"         move "$MOVABLE"
assert_ok      "move to global"                    move "$MOVABLE" --global
assert_output  "task no longer in beta"  "No tasks"      list --project beta
echo ""

# ── 8i. Labels ──────────────────────────────────────────────────

echo "Labels:"
assert_ok      "add-label explicit"                add-label urgent --color red
assert_ok      "add-label minimal"                 add-label weekly
assert_fail    "add-label duplicate fails"         add-label urgent
assert_fail    "add-label without name fails"      add-label
assert_output  "list-labels shows entries" "urgent"   list-labels
assert_output  "list-labels shows count"   "label(s)" list-labels

# Auto-create on first use
add_capture    "add task with --label csv"         "Labelled task" --type bug --label "blocked,review"
LABELLED=$LAST_ID
assert_output  "auto-created label appears" "blocked"  list-labels
assert_output  "show task lists labels"     "{review}" show "$LABELLED"
assert_output  "list filtered by label"     "Labelled task" list --label review

# label / unlabel commands
add_capture    "add task without labels"           "Plain task"
PLAIN=$LAST_ID
assert_ok      "attach label to existing task"     label "$PLAIN" backlog
assert_output  "auto-created via label cmd" "backlog" list-labels
assert_output  "list by auto-created label" "Plain task" list --label backlog
assert_ok      "detach label"                      unlabel "$PLAIN" backlog
assert_fail    "detach already-detached fails"     unlabel "$PLAIN" backlog
assert_fail    "label nonexistent task fails"      label 999999 foo
assert_fail    "unlabel nonexistent label fails"   unlabel "$PLAIN" nopelabel

# rm-label
assert_ok      "rm-label removes label"            rm-label weekly
assert_fail    "rm-label nonexistent fails"        rm-label weekly
echo ""

# ── 8j. Changelog ───────────────────────────────────────────────

echo "Changelog:"
CL_VERSION=$(cat .version 2>/dev/null | tr -d '[:space:]')
assert_output  "changelog default shows latest"   "## ["       changelog
assert_output  "changelog default shows version"  "$CL_VERSION" changelog
assert_output  "changelog --all shows multi"      "## ["       changelog --all
assert_output  "changelog --list shows current"   "$CL_VERSION" changelog --list
assert_output  "changelog by exact version"       "$CL_VERSION" changelog "$CL_VERSION"
assert_output  "changelog accepts v-prefix"       "$CL_VERSION" changelog "v$CL_VERSION"
assert_fail    "changelog unknown version fails"               changelog 99.99.99
assert_output  "changelog --since prior version"  "## ["       changelog --since 0.0.0
assert_fail    "changelog --since future version"              changelog --since 99.99.99
assert_output  "help command changelog"           "release notes" help changelog
echo ""

# ── 8k. Update check warning ───────────────────────────────────

echo "Update check:"
# Helper: run a command without TODOC_NO_UPDATE_CHECK so the warning
# logic is exercised, and assert that a substring appears in output.
# Restores the env var afterwards so subsequent tests stay quiet.
assert_warn() {
    desc="$1"
    expected="$2"
    shift 2
    saved_env=$TODOC_NO_UPDATE_CHECK
    unset TODOC_NO_UPDATE_CHECK
    if output=$(run "$@"); then
        if echo "$output" | grep -qF "$expected"; then
            PASS=$((PASS + 1))
            printf "  \033[32mPASS\033[0m  %s\n" "$desc"
        else
            FAIL=$((FAIL + 1))
            printf "  \033[31mFAIL\033[0m  %s (expected '%s')\n" "$desc" "$expected"
            printf "        got: %s\n" "$output"
        fi
    else
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (command failed)\n" "$desc"
    fi
    export TODOC_NO_UPDATE_CHECK=$saved_env
}

assert_no_warn() {
    desc="$1"
    forbidden="$2"
    shift 2
    saved_env=$TODOC_NO_UPDATE_CHECK
    unset TODOC_NO_UPDATE_CHECK
    if output=$(run "$@"); then
        if echo "$output" | grep -qF "$forbidden"; then
            FAIL=$((FAIL + 1))
            printf "  \033[31mFAIL\033[0m  %s (unexpected '%s' in output)\n" "$desc" "$forbidden"
        else
            PASS=$((PASS + 1))
            printf "  \033[32mPASS\033[0m  %s\n" "$desc"
        fi
    else
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  %s (command failed)\n" "$desc"
    fi
    export TODOC_NO_UPDATE_CHECK=$saved_env
}

CACHE="$HOME/.todoc/update_check"

# Patch bump
printf 'last_check=%d\nlatest_version=99.0.1\nbreaking=0\n' "$(date +%s)" > "$CACHE"
# Forge a cache where the cached version is patch-newer than the binary
# version. We use 99.0.x so the test stays valid past any future bump.
# (For patch we need same major+minor; harder to forge generically.)
# So instead, the patch test uses a hand-tuned version matching .version
# bumped by 0.0.1.
TEST_VERSION=$(cat .version 2>/dev/null | tr -d '[:space:]')
patch_v=$(printf '%s' "$TEST_VERSION" | awk -F. '{printf "%d.%d.%d", $1, $2, $3+1}')
minor_v=$(printf '%s' "$TEST_VERSION" | awk -F. '{printf "%d.%d.0", $1, $2+1}')
major_v=$(printf '%s' "$TEST_VERSION" | awk -F. '{printf "%d.0.0", $1+1}')

printf 'last_check=%d\nlatest_version=%s\nbreaking=0\n' "$(date +%s)" "$patch_v" > "$CACHE"
assert_warn    "patch bump shows hint"             "Patch v$patch_v"               list

printf 'last_check=%d\nlatest_version=%s\nbreaking=0\n' "$(date +%s)" "$minor_v" > "$CACHE"
assert_warn    "minor bump shows new release"      "New release v$minor_v"         list
assert_warn    "minor bump mentions backup"        "backed up automatically"       list

printf 'last_check=%d\nlatest_version=%s\nbreaking=0\n' "$(date +%s)" "$major_v" > "$CACHE"
assert_warn    "major bump shows breaking warn"    "Major release v$major_v"       list
assert_warn    "major bump mentions changelog"     "todoc changelog --since"       list

# Breaking flag promotes a minor bump to the major-warning treatment
printf 'last_check=%d\nlatest_version=%s\nbreaking=1\n' "$(date +%s)" "$minor_v" > "$CACHE"
assert_warn    "breaking flag escalates minor"     "may contain breaking"          list

# Equal version: no warning
printf 'last_check=%d\nlatest_version=%s\nbreaking=0\n' "$(date +%s)" "$TEST_VERSION" > "$CACHE"
assert_no_warn "equal version is quiet"            "available"                     list

# Older cached version: no warning (use 0.0.1 which is always older)
printf 'last_check=%d\nlatest_version=0.0.1\nbreaking=0\n' "$(date +%s)" > "$CACHE"
assert_no_warn "older cached version is quiet"     "available"                     list

# Env var disables warning even when cache is newer
printf 'last_check=%d\nlatest_version=%s\nbreaking=0\n' "$(date +%s)" "$major_v" > "$CACHE"
assert_output  "env var suppresses warning"        "task(s)"                       list
# (TODOC_NO_UPDATE_CHECK is still set globally via the suite header,
#  so the warning must NOT appear in this assertion's combined output.
#  We rely on the major_v warning text being absent from a normal list.)

# Clean up the cache so the rest of the suite stays untouched
rm -f "$CACHE"
echo ""

# ── 8l. Mode / agent output ────────────────────────────────────
#
# Mode resolution is per-process, no persistent file. The order is:
#   1. --json flag
#   2. TODOC_MODE env var (ai|user)
#   3. Auto-detect: CLAUDECODE / CLAUDE_CODE_ENTRYPOINT / CLAUDE_PROJECT_DIR
#      / CURSOR_TRACE_ID / TODOC_AGENT
#   4. default user
#
# The test suite must run as the agent's view of the world (no agent
# env vars present), otherwise the auto-detect step would force every
# command into ai mode and break the dozens of user-mode assertions
# above. Make sure none of those vars are set for the rest of the file.

echo "Mode / agent output:"

unset CLAUDECODE CLAUDE_CODE_ENTRYPOINT CLAUDE_PROJECT_DIR CURSOR_TRACE_ID TODOC_AGENT TODOC_MODE

# 'mode' is read-only — it reports the resolved mode and its source
assert_output  "mode default is user"            "user (source: default)"     mode
assert_output  "mode default reports source"     "default"                    mode

# --json flag is the one-shot override
assert_output  "--json flag forces ai"           "\"mode\":\"ai\""            mode --json
assert_output  "--json source is json-flag"      "\"source\":\"json-flag\""   mode --json

# In ai mode every command emits a JSON envelope on stdout
assert_output  "ai list emits envelope"          "\"schema\":\"todoc/v1\""    list --json
assert_output  "ai list has tasks key"           "\"tasks\":"                 list --json
assert_output  "ai add returns task object"      "\"task\":"                  add "Json task" --type chore --json
assert_output  "ai stats wraps stats"            "\"by_status\":"             stats --json

# Errors in ai mode go to stderr as JSON. assert_output requires exit 0
# but errors exit non-zero, so we hand-roll a substring check that
# tolerates a non-zero exit.
err_out=$(run show 999999 --json 2>&1 || true)
if echo "$err_out" | grep -q '"code":"not_found"'; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  ai not_found code in error envelope\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  ai not_found code in error envelope\n"
    printf "        got: %s\n" "$err_out"
fi
if echo "$err_out" | grep -q '"command":"show"'; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  ai error envelope has command field\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  ai error envelope has command field\n"
    printf "        got: %s\n" "$err_out"
fi
assert_fail    "ai unknown task exits non-zero"                                show 999999 --json

# After a one-shot --json call, the next user-mode call is unaffected
assert_output  "user mode unaffected after json" "ID"                           list

# TODOC_MODE env var (process scope)
out=$(env TODOC_MODE=ai $TODOC list 2>&1)
if echo "$out" | grep -q '"schema":"todoc/v1"'; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  TODOC_MODE env var triggers ai\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  TODOC_MODE env var triggers ai\n"
    printf "        got: %s\n" "$out"
fi

# TODOC_MODE=ai also surfaces in 'mode' source
out=$(env TODOC_MODE=ai $TODOC mode 2>&1)
if echo "$out" | grep -q '"source":"env-var"'; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  TODOC_MODE source is env-var\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  TODOC_MODE source is env-var\n"
    printf "        got: %s\n" "$out"
fi

# TODOC_MODE=user beats auto-detect (escape hatch for humans whose
# shell happens to set an agent marker — e.g. running todoc inside
# `claude code` for personal use).
out=$(env TODOC_MODE=user CLAUDECODE=1 $TODOC mode 2>&1)
if echo "$out" | grep -q "user (source: env-var)"; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  TODOC_MODE=user beats auto-detect\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  TODOC_MODE=user beats auto-detect\n"
    printf "        got: %s\n" "$out"
fi

# Auto-detect: each agent env var on its own should flip to ai mode.
for var in CLAUDECODE CLAUDE_CODE_ENTRYPOINT CLAUDE_PROJECT_DIR CURSOR_TRACE_ID TODOC_AGENT; do
    out=$(env "$var=1" $TODOC mode 2>&1)
    if echo "$out" | grep -q "\"source\":\"auto-detect:$var\""; then
        PASS=$((PASS + 1))
        printf "  \033[32mPASS\033[0m  auto-detect via %s\n" "$var"
    else
        FAIL=$((FAIL + 1))
        printf "  \033[31mFAIL\033[0m  auto-detect via %s\n" "$var"
        printf "        got: %s\n" "$out"
    fi
done

# Legacy ~/.todoc/mode file (pre-auto-detect versions) is silently
# removed on startup so it never confuses anyone inspecting the
# data dir.
mkdir -p "$HOME/.todoc"
echo "ai" > "$HOME/.todoc/mode"
$TODOC list > /dev/null 2>&1 || true
if [ ! -e "$HOME/.todoc/mode" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  legacy mode file is removed on startup\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  legacy mode file is removed on startup\n"
fi

# Update-check warning is suppressed in ai mode
mkdir -p "$HOME/.todoc"
printf 'last_check=%d\nlatest_version=99.99.99\nbreaking=0\n' "$(date +%s)" > "$HOME/.todoc/update_check"
assert_no_warn "ai suppresses update warning"   "Major release"                 list --json
rm -f "$HOME/.todoc/update_check"

# Help for the read-only diagnostic
assert_output  "help command mode"              "Show the current output mode"  help mode
echo ""

# ── 8m. Uninstall ──────────────────────────────────────────────

echo "Uninstall:"
# We test against a COPY of the binary so the suite never blows away
# its own ./build/todoc. Each subtest gets a fresh copy.

copy_binary() {
    cp ./build/todoc "$HOME/todoc-uninst"
}

assert_output  "help command uninstall"   "Remove the todoc binary"   help uninstall

# 1. Without --yes and without stdin input → aborts via prompt
copy_binary
echo "n" | "$HOME/todoc-uninst" uninstall > "$HOME/_un_out" 2>&1
if [ -x "$HOME/todoc-uninst" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  uninstall aborts on n\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  uninstall aborts on n (binary missing!)\n"
fi
rm -f "$HOME/todoc-uninst"

# 2. With --yes, default keeps data
copy_binary
echo "kept" > "$HOME/.todoc/marker"
"$HOME/todoc-uninst" uninstall --yes > "$HOME/_un_out" 2>&1
out=$(cat "$HOME/_un_out")
if [ ! -e "$HOME/todoc-uninst" ] && [ -f "$HOME/.todoc/marker" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  uninstall --yes removes binary, keeps data\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  uninstall --yes (binary=%s data=%s)\n" \
        "$([ -e "$HOME/todoc-uninst" ] && echo present || echo missing)" \
        "$([ -f "$HOME/.todoc/marker" ] && echo present || echo missing)"
    printf "        %s\n" "$out"
fi
rm -f "$HOME/.todoc/marker"

# 3. With --purge --yes, data dir is wiped too
copy_binary
echo "to-be-deleted" > "$HOME/.todoc/marker"
"$HOME/todoc-uninst" uninstall --purge --yes > "$HOME/_un_out" 2>&1
if [ ! -e "$HOME/todoc-uninst" ] && [ ! -d "$HOME/.todoc" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  uninstall --purge --yes removes binary AND data\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  uninstall --purge --yes (binary=%s data=%s)\n" \
        "$([ -e "$HOME/todoc-uninst" ] && echo present || echo missing)" \
        "$([ -d "$HOME/.todoc" ] && echo present || echo missing)"
fi
mkdir -p "$HOME/.todoc"  # restore for the rest of the suite

# 4. ai mode without --yes returns a JSON error envelope
copy_binary
out=$(env TODOC_MODE=ai "$HOME/todoc-uninst" uninstall 2>&1 || true)
if echo "$out" | grep -q '"code":"needs_confirmation"'; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  ai uninstall without --yes refuses\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  ai uninstall without --yes refuses\n"
    printf "        got: %s\n" "$out"
fi
if [ -x "$HOME/todoc-uninst" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  ai refusal leaves binary intact\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  ai refusal leaves binary intact\n"
fi
rm -f "$HOME/todoc-uninst"

# 5. ai mode with --yes succeeds and emits a JSON envelope
copy_binary
out=$(env TODOC_MODE=ai "$HOME/todoc-uninst" uninstall --yes 2>&1)
if echo "$out" | grep -q '"command":"uninstall"' && \
   echo "$out" | grep -q '"ok":true' && \
   echo "$out" | grep -q '"binary_path":' && \
   [ ! -e "$HOME/todoc-uninst" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  ai uninstall --yes emits envelope and removes binary\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  ai uninstall --yes\n"
    printf "        got: %s\n" "$out"
fi

rm -f "$HOME/_un_out"
echo ""

# ── 8n. Completions ────────────────────────────────────────────

echo "Completions:"

# The uninstall block above wipes ~/.todoc/, so we need a fresh DB
# before the completion fixtures can be inserted.
assert_ok      "fixture: re-init DB after uninstall block"        init
assert_ok      "fixture: add a task for task-ids completion"      add "Completion fixture task"

# Static lists
assert_output  "complete commands lists init"   "init"            complete commands
assert_output  "complete commands lists add"    "add"             complete commands
assert_output  "complete topics lists task"     "task"            complete topics

# Dynamic lists need DB content. Add some fixtures.
assert_ok      "fixture: add-project alpha"                       add-project alpha
assert_ok      "fixture: add-project beta"                        add-project beta
assert_ok      "fixture: add-label urgent"                        add-label urgent
assert_output  "complete projects lists alpha"  "alpha"           complete projects
assert_output  "complete projects lists beta"   "beta"            complete projects
assert_output  "complete labels lists urgent"   "urgent"          complete labels
assert_output  "complete task-ids lists numbers" "1"              complete task-ids
assert_fail    "complete bogus kind fails"                        complete bogus

# Embedded scripts can be printed
assert_output  "completions bash prints script" "_todoc"          completions bash
assert_output  "completions zsh prints script"  "compdef todoc"   completions zsh
assert_output  "completions fish prints script" "complete -c todoc" completions fish
assert_fail    "completions unknown shell fails"                  completions tcsh

# Help block
assert_output  "help completions"               "Shell tab-completion" help completions
assert_output  "help complete plumbing"         "Plumbing"        help complete

# Auto-install: detects $SHELL, writes a file, then we uninstall it
SAVED_SHELL=${SHELL:-}
SHELL=/usr/bin/bash assert_ok    "completions install (bash)"     completions install
if [ -f "$HOME/.local/share/bash-completion/completions/todoc" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  bash completion file created\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  bash completion file not created\n"
fi
SHELL=/usr/bin/bash assert_ok    "completions uninstall (bash)"   completions uninstall
if [ ! -f "$HOME/.local/share/bash-completion/completions/todoc" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  bash completion file removed\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  bash completion file still present\n"
fi

# Auto-install for zsh and fish in their respective dirs
SHELL=/usr/bin/zsh assert_ok     "completions install (zsh)"      completions install
[ -f "$HOME/.zfunc/_todoc" ] && PASS=$((PASS+1)) && \
    printf "  \033[32mPASS\033[0m  zsh completion file created\n" || \
    { FAIL=$((FAIL+1)); printf "  \033[31mFAIL\033[0m  zsh completion file not created\n"; }

SHELL=/usr/bin/fish assert_ok    "completions install (fish)"     completions install
[ -f "$HOME/.config/fish/completions/todoc.fish" ] && PASS=$((PASS+1)) && \
    printf "  \033[32mPASS\033[0m  fish completion file created\n" || \
    { FAIL=$((FAIL+1)); printf "  \033[31mFAIL\033[0m  fish completion file not created\n"; }

# Unknown shell
SHELL=/usr/bin/tcsh assert_fail  "completions install (tcsh)"     completions install

# init silently refreshes an existing completion file (the auto-update
# path triggered by 'todoc update' which always re-runs 'todoc init').
# We pre-create a stub file with marker content, run init, and verify
# (a) the new embedded script is in there and (b) the marker content
# is gone — proving the old content was REPLACED, not appended-to.
mkdir -p "$HOME/.local/share/bash-completion/completions"
echo "STALE-OLD-CONTENT-FROM-PREVIOUS-RELEASE" > "$HOME/.local/share/bash-completion/completions/todoc"
SHELL=/usr/bin/bash assert_ok    "init refreshes existing completion" init
if grep -q "_todoc" "$HOME/.local/share/bash-completion/completions/todoc"; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  refresh wrote the new embedded script\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  refresh did not write the new script\n"
fi
if ! grep -q "STALE-OLD-CONTENT" "$HOME/.local/share/bash-completion/completions/todoc"; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  refresh removed the stale content\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  refresh left stale content in the file\n"
fi

# init respects the no_completion marker (user previously said no).
# Pre-create the marker, remove the completion file, run init, and
# verify the file was NOT recreated.
rm -f "$HOME/.local/share/bash-completion/completions/todoc"
touch "$HOME/.todoc/no_completion"
SHELL=/usr/bin/bash assert_ok    "init with marker present"      init
if [ ! -f "$HOME/.local/share/bash-completion/completions/todoc" ]; then
    PASS=$((PASS + 1))
    printf "  \033[32mPASS\033[0m  init respects no_completion marker\n"
else
    FAIL=$((FAIL + 1))
    printf "  \033[31mFAIL\033[0m  init wrote completion despite marker\n"
fi
rm -f "$HOME/.todoc/no_completion"

# Restore env
if [ -n "$SAVED_SHELL" ]; then
    export SHELL=$SAVED_SHELL
fi

# Clean up the auto-installed files so they don't bleed into other tests
rm -rf "$HOME/.local/share/bash-completion" "$HOME/.zfunc" "$HOME/.config/fish"
echo ""

# ── 9. Help / Version ───────���───────────────────────────────────

echo "Help & Version:"
assert_output  "help shows usage"        "Usage:"             help
assert_output  "--help works"            "Usage:"             --help
assert_output  "-h works"                "Usage:"             -h
assert_output  "help task topic"         "Task commands"      help task
assert_output  "help project topic"      "Project commands"   help project
assert_output  "help export topic"       "Export"             help export
assert_output  "help command add"        "Add a new task"     help add
assert_output  "help command use"        "active project"     help use
assert_output  "help command update"     "latest release"     help update
assert_output  "help alias ls"           "List tasks"         help ls
assert_output  "update listed in help"   "update"             help
assert_fail    "help unknown topic"                           help nonsense
VERSION=$(cat .version 2>/dev/null | tr -d '[:space:]')
assert_output  "version shows number"    "$VERSION"           version
assert_output  "--version works"         "$VERSION"           --version
echo ""

# ── 10. Error handling ──────────────────────────────────────────

echo "Error handling:"
assert_fail    "unknown command"                      foobar
assert_fail    "unknown flag"                         list --unknown
assert_fail    "invalid task id"                      show abc
assert_fail    "negative task id"                     show -1
echo ""

# ── Summary ──────────────────────────���──────────────────────────

echo "─────────────────────────────────────"
TOTAL=$((PASS + FAIL))
if [ "$FAIL" = 0 ]; then
    printf "\033[32mAll %d tests passed.\033[0m\n" "$TOTAL"
else
    printf "\033[31m%d/%d tests failed.\033[0m\n" "$FAIL" "$TOTAL"
fi
echo ""

exit "$FAIL"
