export function skillAuthoringPrompt({ directory, scope }) {
  if (typeof directory !== "string" || !directory || directory.includes("\0"))
    throw new Error("Skill directory is unavailable");
  return `帮我创建一个 mdo Skill。保存范围：${scope === "project" ? "当前项目" : "个人技能"}。
目标父目录：${JSON.stringify(directory)}。
先询问我希望解决的任务、触发场景和输出要求，再编写并保存 <skill-id>/SKILL.md；ID 使用小写字母、数字、点、下划线或连字符。不要覆盖已有技能。
SKILL.md 使用 YAML front matter，至少包含 name 和 description，正文写清工作流程、验证和错误处理。按需添加 references/、scripts/、templates/、assets/，用相对于技能目录的路径引用。
支持 $ARGUMENTS 和 \${MDO_SKILL_DIR}；disable-model-invocation: true 表示仅手动选择，user-invocable: false 表示仅模型选择。不要添加未实现的 context/fork/agent/model/hooks 等行为字段。
技能不授予权限，不自动安装依赖，不自动运行导入脚本。先完成内容和静态检查，最后说明文件位置和如何用 $<skill-id> 调用。`;
}

// Dedicated conversation: every pre-existing draft and its attachments survives.
export async function createSkillAuthoringDraft({ createSession, draftStore, projectId, text, check = () => {} }) {
  check();
  const session = await createSession({ project_id: projectId, title: "创建 Skill" });
  check();
  const key = `${session.project_id}/${session.id}`;
  if (!await draftStore.ensureLoaded(key)) throw new Error("Cannot load the Skill authoring draft");
  check();
  const before = draftStore.snapshot(key);
  if (before.text || before.attachments.length) throw new Error("The authoring draft already contains content");
  draftStore.edit(key, text, [], true);
  if (!await draftStore.flush(key)) throw new Error("The Skill authoring draft has not been saved; retry saving it before sending");
  check();
  return session;
}
