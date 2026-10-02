// Prefer an exact user-defined identity before product compatibility aliases.
export function findModel(models, id) {
  return models.find((model) => model.id === id) ??
    models.find((model) => model.aliases?.includes(id)) ?? null;
}
