import { createAskCard } from "../asks/ask-card.js";
import { answerTaskAsk } from "../../state/tasks.js";
import { reconcileCards } from "../../utils/reconcile.js";
import { element } from "../../utils/dom.js";

export function createTaskQuestions({ onChanged }) {
  const node = element("div", { className: "task-questions conversation-dock-stack" });
  const cards = new Map();
  const deciding = new Set();
  const answered = new Set();
  const drafts = new Map();
  return { node, sync(taskId, items, confirmed) {
    const scope = `task/${taskId}/`;
    const live = new Set(items.map((item) => `${scope}${item.id}`));
    if (confirmed) {
      // A confirmed disappearance ends the one-shot guard. Failed reads retain
      // both the previous card and an acknowledged answer's submitted state.
      for (const cache of [drafts, answered])
        for (const key of cache.keys())
          if (key.startsWith(scope) && !live.has(key)) cache.delete(key);
    }
    const ordered = [];
    for (const item of items) {
      const key = `${scope}${item.id}`;
      let card = cards.get(key);
      if (!card) {
        card = createAskCard({ item, key, deciding, answered,
          // Shared keys include the surface to prevent duplicate hint IDs.
          drafts, onAnswer: (value) => answerTaskAsk(taskId, item.id, value),
          onChanged, onSettled: () => {},
        });
        for (const [index, control] of [...card.node.querySelectorAll("input, button, h3")].entries())
          control.dataset.taskFocus = `ask/${key}/${index}`;
        cards.set(key, card);
      } else card.sync();
      ordered.push(card.node);
    }
    for (const key of cards.keys()) if (!live.has(key)) cards.delete(key);
    reconcileCards(node, ordered);
    node.hidden = !items.length;
  } };
}
