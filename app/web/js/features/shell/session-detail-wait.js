// A retry can supersede the request started when the route was selected.
// Wait for the current resource state so an older, slow request cannot keep
// the selected session's queue gate closed after the retry succeeds.
export function waitForSelectedDetail({ navigation, store, isSelected }) {
  return new Promise((resolve) => {
    let settled = false;
    let stopStore = null;
    let stopNavigation = null;
    function finish(state) {
      if (settled) return;
      settled = true;
      stopStore?.();
      stopNavigation?.();
      resolve(state);
    }
    function check() {
      if (!isSelected()) return finish(null);
      const state = store.get();
      if (state.status === "ready" || state.status === "error") finish(state);
    }
    stopStore = store.subscribe(check);
    if (settled) {
      stopStore();
      return;
    }
    stopNavigation = navigation.subscribe(check);
    if (settled) stopNavigation();
    check();
  });
}
