# Generic browser host regressions

These component checks originated in mdo-browser-use at
96f1955ccfcdcd95fabb1c10f2774088f1781333. They now exercise canonical mdo
application sources, rather than a private deployment or source overlays.
The fixtures supply DOM/layout/native delivery peers only. They do not
authorize production Android automation or emulate real-device acceptance.

`tools/qa_release.py` includes these modules in its sequential JavaScript gate.
No Browser Use plugin, extra application/Home, account or model is required.
