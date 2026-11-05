import { element, toast } from "../../utils/dom.js";
import { copyText } from "../../utils/clipboard.js";
import { mountIcons } from "../../components/icons.js";
import { t } from "../../i18n.js";
import { promptContextText } from "./prompt-context.js";

export function messageActionIcon(name) {
  return element("span", { className: "icon", attrs: { "data-icon": name, "aria-hidden": "true" } });
}

// Provisional and recorded messages use the same controls and hit targets.
export function messageActionButton(icon, label, description, actionRef, onClick) {
  const button = element("button", { className: "timeline-action-button", attrs: {
    type: "button", title: label, "aria-label": description, "data-timeline-action": actionRef,
  } }, [messageActionIcon(icon)]);
  button.addEventListener("click", onClick);
  mountIcons(button);
  return button;
}

export function messageCopyButton(resolve, { mainReply = false, actionRef } = {}) {
  const label = mainReply ? t("timeline.copyMainReply", {}, "复制主要回复") : t("timeline.copyMessage", {}, "复制消息");
  let copying = false;
  const button = messageActionButton("copy", label, label, actionRef, async () => {
    if (copying) return;
    copying = true; button.setAttribute("aria-disabled", "true");
    try {
      const resolved = await resolve();
      await copyText(promptContextText(resolved.text));
      toast(resolved.complete ? t("timeline.messageCopied", {}, "消息已复制") : t("timeline.visibleMessageCopied", {}, "已复制可见部分"));
    } catch (error) {
      if (error?.name !== "AbortError") toast(t("timeline.messageCopyFailed", {}, "无法复制消息"), "error");
    } finally { copying = false; button.setAttribute("aria-disabled", "false"); }
  });
  return button;
}
