"""Bounded schedule persistence, claim, completion, and recovery probe."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

PROBE_SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/projects/lifecycle.c"
#include "src/config/config.c"
static void ScheduleProbeResolved(const char *id);
static void ScheduleProbeCatalogResolved(void);
static bool ScheduleProbePublish(const char *path);
static bool ScheduleProbeHistory(const char *path, const char *json,
    size_t size, xwork_error *error);
static bool ScheduleProbeSetEnabled(xwork_runtime *runtime, const char *id,
    bool enabled, xwork_error *error);
#include "src/schedules/manager.c"

static bool ProbeMove;
static bool ProbeCatalogChange;
static bool ProbeHookOk = true;
static bool ProbePublishing;
static unsigned ProbePublishFail;
static unsigned ProbePublishChecks;
static unsigned ProbePublishFailAt;
static unsigned ProbeSettingsChecks;
static bool ProbeSettingsRollback;
static bool ProbeHistoryCheck;
static bool ProbeHistoryFail;
static unsigned ProbeHistoryChecks;

static bool ScheduleLeaseAvailable(const char *project) {
    MdoProjectLease *lease = MdoProjectLeaseAcquire(project,
        MDO_PROJECT_LEASE_EXCLUSIVE, NULL);
    bool available = lease != NULL;
    MdoProjectLeaseRelease(lease);
    return available;
}

static void ScheduleProbeOptions(MdoScheduleCreateOptions *options,
    const char *id, const char *project) {
    MdoScheduleCreateOptionsInit(options);
    options->Id = id;
    options->Label = "Lifecycle probe";
    options->ProjectId = project;
    options->AgentId = "reviewer";
    options->Input = "bounded lifecycle probe";
    options->StartAt = 1700000000000000LL;
}

/* Inject a real intervening definition write at the unlocked resolution
 * boundary, rather than mutating manager fields directly. */
static void ScheduleProbeResolved(const char *id) {
    MdoScheduleCreateOptions options;
    if (!ProbeMove) return;
    ProbeMove = false;
    ScheduleProbeOptions(&options, id, "project-gamma");
    ProbeHookOk = MdoScheduleReplace(id, UINT64_MAX, &options, NULL, NULL);
}

static void ScheduleProbeCatalogResolved(void) {
    MdoScheduleCreateOptions options;
    if (!ProbeCatalogChange) return;
    ProbeCatalogChange = false;
    ScheduleProbeOptions(&options, "scope-added", "project-gamma");
    ProbeHookOk = MdoScheduleCreate(&options, NULL, NULL);
}

static bool ScheduleProbePublish(const char *path) {
    (void)path;
    if (!ProbePublishing) return true;
    ++ProbePublishChecks;
    if (ScheduleLeaseAvailable("project-alpha") ||
        ScheduleLeaseAvailable("project-beta")) ProbeHookOk = false;
    if (ProbePublishFailAt == ProbePublishChecks) return false;
    if (ProbePublishFail != 0u) { --ProbePublishFail; return false; }
    return true;
}

static bool ScheduleProbeHistory(const char *path, const char *json,
    size_t size, xwork_error *error) {
    if (ProbeHistoryCheck) {
        ++ProbeHistoryChecks;
        if (ScheduleLeaseAvailable("project-alpha")) ProbeHookOk = false;
        if (ProbeHistoryFail) {
            MdoSchedulesError(error, XWORK_ERROR_IO, "controlled history failure");
            return false;
        }
    }
    return MdoSchedulesAppendBounded(path, json, size, error);
}

/* Fail the second global update and its rollback. Both the rollback and the
 * fallback disable must still reserve every project in the catalog. */
static bool ScheduleProbeSetEnabled(xwork_runtime *runtime, const char *id,
    bool enabled, xwork_error *error) {
    if (ProbeSettingsRollback) {
        ++ProbeSettingsChecks;
        if (ScheduleLeaseAvailable("project-beta") ||
            ScheduleLeaseAvailable("project-gamma")) ProbeHookOk = false;
        if (ProbeSettingsChecks == 2u || ProbeSettingsChecks == 3u) {
            MdoSchedulesError(error, XWORK_ERROR_CONTEXT, "controlled settings failure");
            return false;
        }
    }
    return xworkRuntimeSetScheduleEnabled(runtime, id, enabled, error);
}

static bool ScheduleLeaseProbe(void) {
    static const char disable[] =
        "{\"schema_version\":1,\"patch\":{\"agent\":{\"schedules\":false}}}";
    static const char restore[] = "{\"schema_version\":1,\"patch\":{}}";
    const char *paths[] = {"schedules/scope-a.json", "schedules/scope-b.json",
        "schedules/audit.jsonl"};
    char *before[3] = {0};
    size_t sizes[3] = {0};
    MdoScheduleCreateOptions options;
    MdoProjectLease *exclusive = NULL;
    MdoHomeSnapshot home;
    xwork_error error;
    xfileinfo stat;
    uint64 generation;
    size_t i;
    bool ok = false;
    ScheduleProbeOptions(&options, "scope-a", "project-alpha");
    exclusive = MdoProjectLeaseAcquire("PROJECT-ALPHA.",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (exclusive == NULL || MdoScheduleCreate(&options, NULL, &error) ||
        error.eCode != XWORK_ERROR_CONTEXT ||
        MdoScheduleCreate(&options, NULL, NULL) ||
        MdoScheduleManagerGeneration() != 1u) goto done;
    memset(&home, 0, sizeof(home)); home.Size = sizeof(home);
    if (!MdoHomeGetSnapshot(&home) || xrtPathStat(home.Path, false, &stat)) goto done;
    printf("schedule_lease_create=1\n");
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    if (!MdoScheduleCreate(&options, NULL, &error)) goto done;
    ScheduleProbeOptions(&options, "scope-b", "project-beta");
    if (!MdoScheduleCreate(&options, NULL, &error)) goto done;
    for (i = 0u; i < 3u; ++i)
        if (!MdoSchedulesRead(paths[i], &before[i], &sizes[i])) goto done;
    generation = MdoScheduleManagerGeneration();
    exclusive = MdoProjectLeaseAcquire("project-alpha",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    ScheduleProbeOptions(&options, "scope-a", "project-beta");
    if (exclusive == NULL ||
        MdoScheduleReplace("scope-a", 1u, &options, NULL, NULL) ||
        MdoScheduleSetEnabled("scope-a", 1u, false, NULL, NULL) ||
        MdoScheduleRemove("scope-a", 1u, NULL) ||
        !MdoScheduleManagerReloadSettings(&error) ||
        MdoScheduleManagerGeneration() != generation) goto done;
    for (i = 0u; i < 3u; ++i) {
        char *after = NULL;
        size_t size = 0u;
        bool same = MdoSchedulesRead(paths[i], &after, &size) &&
            sizes[i] == size && memcmp(before[i], after, size) == 0;
        xrtFree(after);
        if (!same) goto done;
    }
    if (!MdoScheduleSetEnabled("scope-b", 1u, false, NULL, &error)) goto done;
    printf("schedule_lease_writers=1\n");
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    exclusive = MdoProjectLeaseAcquire("project-beta",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    if (exclusive == NULL ||
        MdoScheduleReplace("scope-a", 1u, &options, NULL, &error) ||
        error.eCode != XWORK_ERROR_CONTEXT ||
        !ScheduleLeaseAvailable("project-alpha")) goto done;
    printf("schedule_lease_destination=1\n");
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS, xrtStrView(disable))) goto done;
    generation = MdoScheduleManagerGeneration();
    if (MdoScheduleManagerReloadSettings(&error) ||
        error.eCode != XWORK_ERROR_CONTEXT || !MdoScheduleManagerEnabled() ||
        MdoScheduleManagerGeneration() != generation ||
        !ScheduleLeaseAvailable("project-alpha")) goto done;
    printf("schedule_lease_settings_blocked=1\n");
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS, xrtStrView(restore))) goto done;
    ProbePublishing = true;
    if (!MdoScheduleReplace("scope-a", 1u, &options, NULL, &error) ||
        ProbePublishChecks != 1u || !ProbeHookOk) goto done;
    ScheduleProbeOptions(&options, "scope-a", "project-alpha");
    ProbePublishFail = 1u;
    if (MdoScheduleReplace("scope-a", 2u, &options, NULL, &error) ||
        ProbePublishChecks != 3u || !ProbeHookOk) goto done;
    ProbePublishing = false;
    if (!ScheduleLeaseAvailable("project-alpha") ||
        !ScheduleLeaseAvailable("project-beta")) goto done;
    printf("schedule_lease_replace_rollback=1\n");
    ProbeMove = true;
    if (MdoScheduleSetEnabled("scope-a", UINT64_MAX, false, NULL, &error) ||
        error.eCode != XWORK_ERROR_CONTEXT ||
        strcmp(error.sMessage, "schedule project changed; reload before updating") != 0 ||
        !ProbeHookOk || !ScheduleLeaseAvailable("project-beta") ||
        !ScheduleLeaseAvailable("project-gamma")) goto done;
    printf("schedule_lease_owner_race=1\n");
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS, xrtStrView(disable))) goto done;
    ProbeCatalogChange = true;
    if (MdoScheduleManagerReloadSettings(&error) ||
        error.eCode != XWORK_ERROR_CONTEXT || !MdoScheduleManagerEnabled() ||
        !ProbeHookOk || !ScheduleLeaseAvailable("project-beta") ||
        !ScheduleLeaseAvailable("project-gamma")) goto done;
    printf("schedule_lease_catalog_race=1\n");
    ProbeSettingsRollback = true;
    if (MdoScheduleManagerReloadSettings(&error) ||
        strcmp(error.sMessage, "schedule settings rollback failed; scheduling was disabled") != 0 ||
        MdoScheduleManagerEnabled() || ProbeSettingsChecks != 6u || !ProbeHookOk ||
        !ScheduleLeaseAvailable("project-beta") ||
        !ScheduleLeaseAvailable("project-gamma")) goto done;
    ProbeSettingsRollback = false;
    printf("schedule_lease_settings_rollback=1\n");
    ok = true;
done:
    MdoProjectLeaseRelease(exclusive);
    for (i = 0u; i < 3u; ++i) xrtFree(before[i]);
    return ok;
}

static bool ScheduleExecutionLeaseProbe(bool claim_failure) {
    const int64 start = 1700000000000000LL;
    const char *paths[] = {"schedules/claim-a.json", "schedules/claim-0.json",
        "schedules/audit.jsonl"};
    char *before[3] = {0};
    size_t sizes[3] = {0};
    MdoScheduleCreateOptions options;
    MdoScheduleClaim claim;
    MdoProjectLease *exclusive = NULL;
    xwork_error error;
    size_t i;
    bool ok = false;
    /* The missed occurrence sorts before the actual claim when times tie:
     * one call advances this project and claims another in the same pass. */
    ScheduleProbeOptions(&options, "claim-0", "project-beta");
    options.Frequency = XWORK_SCHEDULE_MINUTELY;
    options.MisfirePolicy = XWORK_SCHEDULE_MISFIRE_SKIP;
    options.MisfireGraceSeconds = 1u;
    if (!MdoScheduleCreate(&options, NULL, &error)) goto done;
    ScheduleProbeOptions(&options, "claim-a", "project-alpha");
    if (!MdoScheduleCreate(&options, NULL, &error)) goto done;
    for (i = 0u; i < 3u; ++i)
        if (!MdoSchedulesRead(paths[i], &before[i], &sizes[i])) goto done;
    exclusive = MdoProjectLeaseAcquire("PROJECT-BETA.",
        MDO_PROJECT_LEASE_EXCLUSIVE, &error);
    MdoScheduleClaimInit(&claim);
    if (exclusive == NULL ||
        !MdoScheduleClaimDue(start - 1, &claim, &error) || claim.Claimed ||
        MdoScheduleClaimDue(start + 300000000LL, &claim, &error) ||
        error.eCode != XWORK_ERROR_CONTEXT || claim.Claimed ||
        MdoScheduleTrigger("claim-a", 1u, start, &claim, NULL)) goto done;
    for (i = 0u; i < 3u; ++i) {
        char *after = NULL;
        size_t size = 0u;
        bool same = MdoSchedulesRead(paths[i], &after, &size) &&
            sizes[i] == size && memcmp(before[i], after, size) == 0;
        xrtFree(after);
        if (!same) goto done;
    }
    MdoProjectLeaseRelease(exclusive); exclusive = NULL;
    if (!ScheduleLeaseAvailable("project-alpha")) goto done;
    printf("schedule_claim_exclusion=1\n");
    ProbePublishing = true;
    ProbePublishFailAt = claim_failure ? 2u : 0u;
    MdoScheduleClaimInit(&claim);
    if (claim_failure) {
        if (MdoScheduleClaimDue(start + 300000000LL, &claim, &error) ||
            claim.Claimed || ProbePublishChecks != 2u || !ProbeHookOk ||
            ScheduleLeaseAvailable("project-alpha") ||
            ScheduleLeaseAvailable("project-beta")) goto done;
        MdoScheduleManagerUnit();
        if (!ScheduleLeaseAvailable("project-alpha") ||
            !ScheduleLeaseAvailable("project-beta")) goto done;
        printf("schedule_claim_failure_isolation=1\n");
        ok = true; goto done;
    }
    if (!MdoScheduleClaimDue(start + 300000000LL, &claim, &error) ||
        !claim.Claimed || strcmp(claim.ProjectId, "project-alpha") != 0 ||
        ProbePublishChecks != 2u || !ProbeHookOk ||
        ScheduleLeaseAvailable("project-alpha") ||
        !ScheduleLeaseAvailable("project-beta")) goto done;
    ProbePublishing = false;
    printf("schedule_claim_handoff=1\n");
    ProbeHistoryCheck = true;
    if (!MdoScheduleFinishTask(claim.TaskId, XWORK_RESULT_OK, "claim completed", &error) ||
        ProbeHistoryChecks != 1u || !ProbeHookOk ||
        !ScheduleLeaseAvailable("project-alpha") ||
        MdoScheduleFinishTask(claim.TaskId, XWORK_RESULT_OK, "duplicate", NULL)) goto done;
    printf("schedule_claim_finish=1\n");
    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleTrigger("claim-a", 2u, start, &claim, &error) ||
        !xworkRuntimeCancelTask(g_MdoSchedules.Runtime, claim.TaskId, &error) ||
        ScheduleLeaseAvailable("project-alpha") ||
        !MdoScheduleFinishTask(claim.TaskId, XWORK_RESULT_OK, "cancel observed", &error) ||
        !ScheduleLeaseAvailable("project-alpha")) goto done;
    printf("schedule_claim_cancel=1\n");
    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleTrigger("claim-a", 3u, start, &claim, &error)) goto done;
    ProbeHistoryFail = true;
    if (MdoScheduleFinishTask(claim.TaskId, XWORK_RESULT_OK, "history failed", &error) ||
        error.eCode != XWORK_ERROR_IO || ScheduleLeaseAvailable("project-alpha") ||
        !ScheduleLeaseAvailable("project-beta") ||
        MdoScheduleFinishTask(claim.TaskId, XWORK_RESULT_OK, "retry", &error) ||
        error.eCode != XWORK_ERROR_IO || ProbeHistoryChecks != 3u) goto done;
    MdoScheduleManagerUnit();
    if (!ScheduleLeaseAvailable("project-alpha")) goto done;
    printf("schedule_claim_history_isolation=1\n");
    ok = true;
done:
    MdoProjectLeaseRelease(exclusive);
    for (i = 0u; i < 3u; ++i) xrtFree(before[i]);
    return ok;
}

static void PrintCatalog(const char *label) {
    xwork_error error;
    MdoScheduleCatalog *catalog = MdoScheduleCatalogSnapshot(&error);
    MdoScheduleInfo info;
    printf("%s=count:%zu diagnostics:%zu generation:%llu enabled:%d code:%d\n",
        label, MdoScheduleCatalogCount(catalog),
        MdoScheduleCatalogDiagnosticCount(catalog),
        (unsigned long long)MdoScheduleCatalogGeneration(catalog),
        MdoScheduleManagerEnabled() ? 1 : 0, (int)error.eCode);
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (MdoScheduleCatalogAt(catalog, 0u, &info))
        printf("schedule=id:%s revision:%llu runtime:%llu next:%lld claims:%llu active:%zu runnable:%d\n",
            info.Id, (unsigned long long)info.Revision,
            (unsigned long long)info.RuntimeGeneration,
            (long long)info.NextOccurrenceAt,
            (unsigned long long)info.ClaimCount, info.ActiveRuns,
            info.Runnable ? 1 : 0);
    MdoScheduleCatalogRelease(catalog);
}

void ServiceInit(XS_HostInfo *host) {
    const int64 start = 1700000000000000LL;
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    MdoScheduleCreateOptions options;
    MdoScheduleInfo info;
    MdoScheduleClaim claim;
    xwork_error error;
    char long_label[300];
    static const char invalid[] = "{}";
    static const char disable_schedules[] =
        "{\"schema_version\":1,\"patch\":{\"agent\":{\"schedules\":false}}}";
    static const char restore_settings[] =
        "{\"schema_version\":1,\"patch\":{}}";
    bool result;
    (void)host;

    if (!MdoHomeInit() || !MdoProjectLifecycleInit() || !MdoConfigInit()) {
        printf("init_error=pre-runtime\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoScheduleManagerInit(runtime)) {
        printf("init_error=runtime\n"); goto done;
    }
    PrintCatalog("catalog_empty");
    if (getenv("MDO_SCHEDULE_EXECUTION_ONLY") != NULL) {
        bool fail = getenv("MDO_SCHEDULE_CLAIM_FAILURE") != NULL;
        if (!ScheduleExecutionLeaseProbe(fail)) printf("execution_lease_error=1\n");
        printf("probe_done=1\n"); goto done;
    }
    if (getenv("MDO_SCHEDULE_LEASE_ONLY") != NULL) {
        if (!ScheduleLeaseProbe()) printf("lease_probe_error=1\n");
        printf("probe_done=1\n"); goto done;
    }
    if (getenv("MDO_SCHEDULE_EMPTY_ONLY") != NULL) {
        printf("probe_done=1\n"); goto done;
    }

    MdoScheduleCreateOptionsInit(&options);
    options.Id = "daily-review";
    options.Label = "Daily review";
    options.Notify = "desktop";
    options.ProjectId = "project-alpha";
    options.AgentId = "reviewer";
    options.ModelId = "ling-3.0-tiny";
    options.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    options.ReasoningEffort = "medium";
    options.MaxOutputTokens = 2048u;
    options.WorkspaceRoot = "D:/work/project-alpha";
    options.Input = "review the private release notes";
    options.StartAt = start;
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (!MdoScheduleCreate(&options, &info, &error)) {
        printf("create_error=%s\n", error.sMessage); goto done;
    }
    printf("created=id:%s revision:%llu next:%lld protocol:%d\n", info.Id,
        (unsigned long long)info.Revision, (long long)info.NextOccurrenceAt,
        (int)info.Protocol);
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS,
            xrtStrView(disable_schedules)) ||
        !MdoScheduleManagerReloadSettings(&error)) {
        printf("settings_disable_error=%s\n", error.sMessage); goto done;
    }
    PrintCatalog("catalog_runtime_disabled");
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS,
            xrtStrView(restore_settings)) ||
        !MdoScheduleManagerReloadSettings(&error)) {
        printf("settings_restore_error=%s\n", error.sMessage); goto done;
    }
    PrintCatalog("catalog_runtime_restored");
    if (getenv("MDO_SCHEDULE_DISABLED_ONLY") != NULL) {
        PrintCatalog("catalog_disabled");
        MdoScheduleClaimInit(&claim);
        if (!MdoScheduleClaimDue(start, &claim, &error)) {
            printf("disabled_claim_error=%s\n", error.sMessage); goto done;
        }
        printf("disabled_claim=claimed:%d wake:%lld\n",
            claim.Claimed ? 1 : 0, (long long)claim.NextWakeAt);
        printf("probe_done=1\n"); goto done;
    }

    options.Id = "remove-me"; options.Label = "Remove me";
    if (!MdoScheduleCreate(&options, NULL, &error)) {
        printf("remove_create_error=%s\n", error.sMessage); goto done;
    }
    result = MdoScheduleRemove("remove-me", 0u, &error);
    printf("stale_remove=%d code:%d\n", result ? 1 : 0, (int)error.eCode);
    if (!MdoScheduleRemove("remove-me", 1u, &error)) {
        printf("remove_error=%s\n", error.sMessage); goto done;
    }
    printf("removed=1\n");

    memset(long_label, 'x', sizeof(long_label)); long_label[sizeof(long_label)-1u] = '\0';
    options.Id = "truncated-label"; options.Label = long_label;
    result = MdoScheduleCreate(&options, NULL, &error);
    printf("overlong_create=%d code:%d\n", result ? 1 : 0, (int)error.eCode);

    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    result = MdoScheduleSetEnabled("daily-review", 0u, false, &info, &error);
    printf("stale_update=%d code:%d\n", result ? 1 : 0, (int)error.eCode);
    if (!MdoScheduleSetEnabled("daily-review", 1u, false, &info, &error) ||
        !MdoScheduleSetEnabled("daily-review", 2u, true, &info, &error)) {
        printf("enable_error=%s\n", error.sMessage); goto done;
    }
    printf("enabled=revision:%llu value:%d runnable:%d\n",
        (unsigned long long)info.Revision, info.Enabled ? 1 : 0,
        info.Runnable ? 1 : 0);

    options.Id = "other-id";
    options.Label = "Updated review";
    options.Input = "review the updated private release notes";
    result = MdoScheduleReplace("daily-review", 3u, &options, &info, &error);
    printf("mismatched_replace=%d code:%d\n", result ? 1 : 0,
        (int)error.eCode);
    options.Id = "daily-review";
    result = MdoScheduleReplace("daily-review", 2u, &options, &info, &error);
    printf("stale_replace=%d code:%d\n", result ? 1 : 0,
        (int)error.eCode);
    if (!MdoScheduleReplace("daily-review", 3u, &options, &info, &error)) {
        printf("replace_error=%s\n", error.sMessage); goto done;
    }
    printf("replaced=revision:%llu runtime:%llu label:%s input:%s\n",
        (unsigned long long)info.Revision,
        (unsigned long long)info.RuntimeGeneration,
        info.Label, info.Input);

    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleClaimDue(start - 1, &claim, &error)) {
        printf("early_claim_error=%s\n", error.sMessage); goto done;
    }
    printf("early_claim=claimed:%d wake:%lld\n", claim.Claimed ? 1 : 0,
        (long long)claim.NextWakeAt);
    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleClaimDue(start, &claim, &error) || !claim.Claimed) {
        printf("claim_error=%s\n", error.sMessage); goto done;
    }
    printf("claimed=id:%s task:%llu occurrence:%lld revision:%llu agent:%s model:%s input:%s\n",
        claim.ScheduleId, (unsigned long long)claim.TaskId,
        (long long)claim.OccurrenceAt,
        (unsigned long long)claim.DefinitionRevision, claim.AgentId,
        claim.ModelId, claim.Input);
    if (!MdoScheduleFinishTask(claim.TaskId, XWORK_RESULT_OK,
            "review completed", &error)) {
        printf("finish_error=%s\n", error.sMessage); goto done;
    }
    printf("finished=1\n");
    PrintCatalog("catalog_claimed");

    MdoScheduleCreateOptionsInit(&options);
    options.Id = "skip-missed";
    options.Label = "Skip missed";
    options.ProjectId = "project-alpha";
    options.AgentId = "reviewer";
    options.Input = "skip this missed occurrence";
    options.Frequency = XWORK_SCHEDULE_MINUTELY;
    options.StartAt = start;
    options.MisfirePolicy = XWORK_SCHEDULE_MISFIRE_SKIP;
    options.MisfireGraceSeconds = 1u;
    if (!MdoScheduleCreate(&options, NULL, &error)) {
        printf("skip_create_error=%s\n", error.sMessage); goto done;
    }
    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleClaimDue(start + 300000000LL, &claim, &error)) {
        printf("skip_claim_error=%s\n", error.sMessage); goto done;
    }
    {
        MdoScheduleCatalog *catalog = MdoScheduleCatalogSnapshot(&error);
        memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
        if (catalog == NULL ||
            !MdoScheduleCatalogFind(catalog, "skip-missed", &info)) {
            MdoScheduleCatalogRelease(catalog);
            printf("skip_catalog_error=1\n"); goto done;
        }
        printf("skip_advanced=claimed:%d revision:%llu next:%lld misfires:%llu\n",
            claim.Claimed ? 1 : 0, (unsigned long long)info.Revision,
            (long long)info.NextOccurrenceAt,
            (unsigned long long)info.MisfireCount);
        MdoScheduleCatalogRelease(catalog);
    }
    if (!MdoScheduleRemove("skip-missed", 2u, &error)) {
        printf("skip_remove_error=%s\n", error.sMessage); goto done;
    }

    MdoScheduleManagerUnit();
    xworkRuntimeRelease(runtime); runtime = NULL;
    if (!MdoHomeAtomicWrite("schedules/broken.json", invalid,
            sizeof(invalid) - 1u, false)) {
        printf("corrupt_error=write\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoScheduleManagerInit(runtime)) {
        printf("recover_error=%s\n", error.sMessage); goto done;
    }
    PrintCatalog("catalog_recovered");
    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleClaimDue(start + 1000000LL, &claim, &error)) {
        printf("exhausted_error=%s\n", error.sMessage); goto done;
    }
    printf("exhausted=claimed:%d wake:%lld\n", claim.Claimed ? 1 : 0,
        (long long)claim.NextWakeAt);
    printf("probe_done=1\n");
done:
    MdoScheduleManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoConfigUnit();
    MdoProjectLifecycleUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in ("web", "default-home/config", "src/storage", "src/config",
                     "src/projects", "src/schedules", "include/mdo"):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c", "src/storage/home_restore.inc.c",
        "src/projects/lifecycle.c",
        "src/config/config.c",
        "src/schedules/manager.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    shutil.copy2(ROOT / "app/src/schedules/internal.h",
                 site / "src/schedules/internal.h")
    manager = site / "src/schedules/manager.c"
    source = manager.read_text(encoding="utf-8")
    # Hooks exist only in this copied fixture, never in the packed app.
    old = '    Ok = MdoHomeAtomicWrite(Path, Json, Size, true);'
    assert source.count(old) == 1
    source = source.replace(old,
        '    Ok = ScheduleProbePublish(Path) && MdoHomeAtomicWrite(Path, Json, Size, true);')
    old = '    Ok = MdoSchedulesAppendBounded(Path, Json, Size, Error);'
    assert source.count(old) == 1
    source = source.replace(old, '    Ok = ScheduleProbeHistory(Path, Json, Size, Error);')
    old = '    xrtMutexUnlock(g_MdoSchedules.Lock);\n    Scope->Leases[0]'
    assert source.count(old) == 1
    source = source.replace(old,
        '    xrtMutexUnlock(g_MdoSchedules.Lock);\n    ScheduleProbeResolved(ScheduleId);\n    Scope->Leases[0]')
    old = '    xrtMutexUnlock(g_MdoSchedules.Lock);\n    for ( i = 0u; i < Count; ++i )'
    assert source.count(old) == 1
    source = source.replace(old,
        '    xrtMutexUnlock(g_MdoSchedules.Lock);\n    ScheduleProbeCatalogResolved();\n    for ( i = 0u; i < Count; ++i )')
    source = source.replace('xworkRuntimeSetScheduleEnabled(', 'ScheduleProbeSetEnabled(')
    manager.write_text(source, encoding="utf-8")
    for name in ("home.h", "home_import.h", "home_purge.h", "home_restore.h", "session_file_policy.h", "config.h", "models.h", "schedules.h",
                 "project_lifecycle.h"):
        shutil.copy2(ROOT / "app/include/mdo" / name, site / "include/mdo" / name)
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "schedule-probe",
        "ip": "127.0.0.1", "port": port,
        "host_default": {"enabled": True, "name": "probe", "path": "web",
                         "devlang": "c", "devfile": "probe.c"},
    }]}), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path, mode: str = "") -> str:
    env = os.environ.copy()
    if mode == "empty":
        env["MDO_SCHEDULE_EMPTY_ONLY"] = "1"
    elif mode == "disabled":
        env["MDO_SCHEDULE_DISABLED_ONLY"] = "1"
    elif mode == "lease":
        env["MDO_SCHEDULE_LEASE_ONLY"] = "1"
    elif mode in ("execution", "claim-failure"):
        env["MDO_SCHEDULE_EXECUTION_ONLY"] = "1"
        if mode == "claim-failure":
            env["MDO_SCHEDULE_CLAIM_FAILURE"] = "1"
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)], cwd=site,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace", env=env,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    lines: list[str] = []
    done = threading.Event()
    assert process.stdout is not None

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.append(line)
            if "probe_done=1" in line:
                done.set()

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    done.wait(timeout=20.0)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)
    reader.join(timeout=3.0)
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="schedule-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        empty_home = base / "empty-home"
        home = base / "home"
        write_site(site)
        empty_output = run_probe(host, site, empty_home, mode="empty")
        assert "catalog_empty=count:0 diagnostics:0 generation:1 enabled:1 code:0" in empty_output, empty_output
        assert "probe_done=1" in empty_output, empty_output
        assert not empty_home.exists(), list(empty_home.rglob("*")) if empty_home.exists() else ""

        lease_output = run_probe(host, site, base / "lease-home", mode="lease")
        assert "lease_probe_error=" not in lease_output, lease_output
        for label in ("create", "writers", "destination", "settings_blocked",
                      "replace_rollback", "owner_race", "catalog_race", "settings_rollback"):
            assert f"schedule_lease_{label}=1" in lease_output, lease_output

        execution_home = base / "execution-home"
        execution_output = run_probe(host, site, execution_home, mode="execution")
        assert "execution_lease_error=" not in execution_output, execution_output
        for label in ("exclusion", "handoff", "finish", "cancel", "history_isolation"):
            assert f"schedule_claim_{label}=1" in execution_output, execution_output
        execution_history = [json.loads(line) for line in
            (execution_home / "schedules/history/claim-a.jsonl").read_text(
                encoding="utf-8").splitlines()]
        assert len(execution_history) == 2, execution_history
        assert [item["result"] for item in execution_history] == [0, -2]
        failure_home = base / "claim-failure-home"
        failure_output = run_probe(host, site, failure_home, mode="claim-failure")
        assert "execution_lease_error=" not in failure_output, failure_output
        assert "schedule_claim_failure_isolation=1" in failure_output, failure_output
        assert json.loads((failure_home / "schedules/claim-0.json").read_text(
            encoding="utf-8"))["revision"] == 2
        assert json.loads((failure_home / "schedules/claim-a.json").read_text(
            encoding="utf-8"))["revision"] == 1

        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "create_error=" not in output, output
        assert "enable_error=" not in output, output
        assert "claim_error=" not in output, output
        assert "finish_error=" not in output, output
        assert "recover_error=" not in output, output
        assert "catalog_empty=count:0 diagnostics:0 generation:1 enabled:1 code:0" in output, output
        assert "created=id:daily-review revision:1 next:1700000000000000 protocol:2" in output, output
        assert "catalog_runtime_disabled=count:1 diagnostics:0 generation:3 enabled:0 code:0" in output, output
        assert "catalog_runtime_restored=count:1 diagnostics:0 generation:4 enabled:1 code:0" in output, output
        assert "stale_remove=0 code:7" in output and "removed=1" in output, output
        assert "overlong_create=0 code:1" in output, output
        assert "stale_update=0 code:7" in output, output
        assert "enabled=revision:3 value:1 runnable:1" in output, output
        assert "mismatched_replace=0 code:1" in output, output
        assert "stale_replace=0 code:7" in output, output
        assert "replaced=revision:4" in output and "label:Updated review" in output, output
        assert "input:review the updated private release notes" in output, output
        assert "early_claim=claimed:0 wake:1700000000000000" in output, output
        assert "claimed=id:daily-review" in output and "occurrence:1700000000000000 revision:5" in output, output
        assert "agent:reviewer model:ling-3.0-tiny input:review the updated private release notes" in output, output
        assert "finished=1" in output, output
        assert "skip_advanced=claimed:0 revision:2 next:1700000360000000 misfires:1" in output, output
        assert "catalog_claimed=count:1 diagnostics:0" in output, output
        assert "revision:5" in output and "next:0 claims:1 active:0 runnable:1" in output, output
        assert "catalog_recovered=count:1 diagnostics:1 generation:1 enabled:1 code:0" in output, output
        assert "exhausted=claimed:0 wake:0" in output, output
        assert "probe_done=1" in output, output

        definition = json.loads((home / "schedules/daily-review.json").read_text(
            encoding="utf-8"))
        assert definition["schema_version"] == 1
        assert definition["revision"] == 5
        assert definition["claim_count"] == 1
        assert definition["next_occurrence_at_us"] == 0
        assert definition["protocol"] == "openai-responses"
        assert definition["input"] == "review the updated private release notes"
        history = [json.loads(line) for line in
                   (home / "schedules/history/daily-review.jsonl").read_text(
                       encoding="utf-8").splitlines()]
        assert len(history) == 1 and history[0]["text"] == "review completed"
        audit_lines = (home / "schedules/audit.jsonl").read_text(
            encoding="utf-8").splitlines()
        audit = [json.loads(line) for line in audit_lines]
        assert [item["operation"] for item in audit] == [
            "create", "create", "remove", "set-enabled", "set-enabled",
            "replace", "claim",
            "create", "advance", "remove"]
        assert all(item["phase"] == "prepared" for item in audit)
        assert all("input" not in item for item in audit)
        assert audit[0]["input_sha256"] == hashlib.sha256(
            b"review the private release notes").hexdigest()
        assert "review the private release notes" not in "\n".join(audit_lines)

        disabled_site = base / "disabled-site"
        disabled_home = base / "disabled-home"
        write_site(disabled_site)
        defaults_path = disabled_site / "default-home/config/defaults.json"
        defaults = json.loads(defaults_path.read_text(encoding="utf-8"))
        defaults["settings"]["agent"]["schedules"] = False
        defaults_path.write_text(json.dumps(defaults), encoding="utf-8")
        disabled = run_probe(host, disabled_site, disabled_home, mode="disabled")
        assert "init_error=" not in disabled and "create_error=" not in disabled, disabled
        assert "catalog_empty=count:0 diagnostics:0 generation:1 enabled:0 code:0" in disabled, disabled
        assert "catalog_disabled=count:1 diagnostics:0" in disabled, disabled
        assert "active:0 runnable:0" in disabled, disabled
        assert "disabled_claim=claimed:0 wake:0" in disabled, disabled
        assert "probe_done=1" in disabled, disabled
    print("schedule runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
