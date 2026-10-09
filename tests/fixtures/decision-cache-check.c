/* Direct, bounded receipt checks in a disposable xs/TCC copy. No HTTP load.
 * The copied cache uses a local clock/token while this function runs; normal
 * host clocks and write admission resume before requests are published. */
static void DecisionCacheCheck(XS_HostInfo* Host)
{
    MdoApiContext Context = {0};
    MdoApiDecisionAttempt Attempt;
    char Path[256], Value[1026];
    size_t i;
    unsigned Checks = 0u;
    bool Ok = true;
#define CHECK(Condition) do { ++Checks; if (!(Condition)) { \
    fprintf(stderr, "decision_cache_check_failed=%u\n", Checks); Ok = false; goto done; } } while (0)
    (void)Host;
    MdoApiDecisionsUnit();
    CHECK(MdoApiDecisionsInit());
    g_DecisionCacheTestEnabled = true;
    g_DecisionCacheTestNow = UINT64_C(1000000);
    g_DecisionCacheTestToken = "generation-a";
    Context.Target.Path = XRT_STR_LITERAL("/approvals/1");
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_NEW);
    MdoApiDecisionEnd(&Attempt, false);
    CHECK(MdoApiDecisionBegin(&Context, "deny", &Attempt) == MDO_API_DECISION_NEW);
    MdoApiDecisionEnd(&Attempt, true);
    CHECK(MdoApiDecisionBegin(&Context, "deny", &Attempt) == MDO_API_DECISION_REPLAY);
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_CONFLICT);
    g_DecisionCacheTestToken = "generation-b";
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_NEW);
    MdoApiDecisionEnd(&Attempt, true);
    g_DecisionCacheTestToken = "generation-a";
    CHECK(MdoApiDecisionBegin(&Context, "deny", &Attempt) == MDO_API_DECISION_REPLAY);
    g_DecisionCacheTestNow += UINT64_C(119000000);
    CHECK(MdoApiDecisionBegin(&Context, "deny", &Attempt) == MDO_API_DECISION_REPLAY);
    g_DecisionCacheTestNow += UINT64_C(1000000);
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_NEW);
    MdoApiDecisionEnd(&Attempt, false);
    MdoApiDecisionsUnit();
    CHECK(MdoApiDecisionsInit());
    /* Fill only the fixed 128-slot memory boundary, without tool execution. */
    for (i = 1u; i <= 128u; ++i) {
        snprintf(Path, sizeof(Path), "/approvals/%zu", i);
        Context.Target.Path = xrtStrView(Path);
        CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_NEW);
        MdoApiDecisionEnd(&Attempt, true);
    }
    Context.Target.Path = XRT_STR_LITERAL("/approvals/129");
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_UNAVAILABLE);
    Context.Target.Path = XRT_STR_LITERAL("/approvals/1");
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_REPLAY);
    g_DecisionCacheTestNow += UINT64_C(120000000);
    Context.Target.Path = XRT_STR_LITERAL("/approvals/129");
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_NEW);
    MdoApiDecisionEnd(&Attempt, false);
    CHECK(MdoApiDecisionBegin(NULL, "allow", &Attempt) == MDO_API_DECISION_UNAVAILABLE);
    CHECK(MdoApiDecisionBegin(&Context, "allow", NULL) == MDO_API_DECISION_UNAVAILABLE);
    Context.Target.Path.Data = NULL;
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_UNAVAILABLE);
    Context.Target.Path = xrtStrViewN("/approvals/1\0other", 18u);
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_UNAVAILABLE);
    memset(Path, 'a', sizeof(Path));
    Context.Target.Path = xrtStrViewN(Path, sizeof(Path));
    CHECK(MdoApiDecisionBegin(&Context, "allow", &Attempt) == MDO_API_DECISION_UNAVAILABLE);
    Context.Target.Path = XRT_STR_LITERAL("/approvals/1");
    memset(Value, 'a', sizeof(Value));
    CHECK(MdoApiDecisionBegin(&Context, Value, &Attempt) == MDO_API_DECISION_UNAVAILABLE);
    CHECK(MdoApiDecisionBegin(&Context, "", &Attempt) == MDO_API_DECISION_UNAVAILABLE);
    Value[1024] = '\0';
    CHECK(MdoApiDecisionBegin(&Context, Value, &Attempt) == MDO_API_DECISION_NEW);
    MdoApiDecisionEnd(&Attempt, true);
    MdoApiDecisionsUnit();
    CHECK(MdoApiDecisionsInit());
    CHECK(MdoApiDecisionBegin(&Context, "deny", &Attempt) == MDO_API_DECISION_NEW);
    MdoApiDecisionEnd(&Attempt, false);
done:
    g_DecisionCacheTestEnabled = false;
    MdoApiDecisionsUnit();
    (void)MdoApiDecisionsInit();
    printf("decision_cache_checks=%u passed=%u\n", Checks, Ok ? 1u : 0u);
    fflush(stdout);
#undef CHECK
}
