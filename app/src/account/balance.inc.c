/* Billing amounts use CNY micro-units, independent of daily token allowance.
 * Copy only verified account totals; arbitrary service data never reaches UI. */
static xvalue* MdoAccountBalance(const xvalue* Data, uint64 MemberId)
{
    static const char* const Fields[] = { "cash_micros", "cash_reserved_micros",
        "credit_micros", "credit_reserved_micros", "available_micros" };
    uint64 Id, Scale, Amounts[5];
    cstr Currency = MdoAccountText(Data, "currency", 3u);
    size_t Index;
    if ( !Currency || strcmp(Currency, "CNY") != 0 ||
         !MdoAccountGetUInt(xrtValueObjectGet(Data, XRT_STR_LITERAL("member_id")), &Id) ||
         Id != MemberId || !MdoAccountGetUInt(xrtValueObjectGet(Data,
            XRT_STR_LITERAL("amount_scale")), &Scale) || Scale != 1000000u ) return NULL;
    for ( Index = 0u; Index < 5u; ++Index )
        if ( !MdoAccountGetUInt(xrtValueObjectGet(Data, xrtStrView(Fields[Index])), &Amounts[Index]) ||
             Amounts[Index] > (Index == 4u ? 2000000000000000ULL : 1000000000000000ULL) ) return NULL;
    if ( Amounts[1] > Amounts[0] || Amounts[3] > Amounts[2] ||
         Amounts[4] != Amounts[0] - Amounts[1] + Amounts[2] - Amounts[3] ) return NULL;
    xvalue* Balance = xrtValueObject();
    bool Ok = Balance && MdoAccountSetUInt(Balance, "member_id", Id) &&
        MdoAccountSetString(Balance, "currency", Currency) &&
        MdoAccountSetUInt(Balance, "amount_scale", Scale) &&
        MdoAccountSetUInt(Balance, "updated_at", (uint64)(xrtNow() / 1000000));
    for ( Index = 0u; Ok && Index < 5u; ++Index )
        Ok = MdoAccountSetUInt(Balance, Fields[Index], Amounts[Index]);
    if ( !Ok ) { xrtValueRelease(Balance); return NULL; }
    return Balance;
}

static void MdoAccountClearBalanceLocked(void)
{
    xrtValueRelease(g_MdoAccount.Balance);
    g_MdoAccount.Balance = NULL;
    g_MdoAccount.BalanceStatus = 0u;
}
