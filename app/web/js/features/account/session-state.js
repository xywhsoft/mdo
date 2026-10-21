// A pending service renewal keeps the remembered identity visible offline.
// Only an explicit expired/signed-out snapshot should present a login action.
export function isAccountSignedIn(snapshot) {
  return snapshot?.state === "signed_in" ||
    (snapshot?.state === "refreshing" && snapshot.refresh_available === true) ||
    (snapshot?.state === "authorizing" && Boolean(snapshot.profile));
}
