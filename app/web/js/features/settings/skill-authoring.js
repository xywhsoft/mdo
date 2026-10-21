import { api } from "../../api/client.js";
import { portableId } from "./extension-formats.js";

export function skillAuthoringPrompt({ directory, scope, mode = "create", id = "", request = "" }) {
  if (typeof directory !== "string" || !directory || directory.includes("\0"))
    throw new Error("Skill directory is unavailable");
  if (!["user", "project"].includes(scope)) throw new Error("请选择个人或项目范围");
  if (!["create", "improve", "review"].includes(mode)) throw new Error("Unknown Skill builder mode");
  if ((mode !== "create" || id) && (!portableId(id) || !/^[a-z]/.test(id))) throw new Error("请填写有效的 Skill ID");
  if (typeof request !== "string" || request.includes("\0") || new TextEncoder().encode(request).length > 8192)
    throw new Error("需求描述最多 8 KiB，且不能包含空字符");
  const action = ({ create: "创建一个", improve: "改进已有", review: "只读检查已有" })[mode];
  return `请使用 Skill 构建器，帮我${action} mdo Skill。保存范围：${scope === "project" ? "当前项目" : "个人技能"}。
目标父目录：${JSON.stringify(directory)}。
${id ? `Skill ID：${JSON.stringify(id)}。` : "请根据用途选择一个未使用的 Skill ID。"}
${request.trim() ? `用户需求：${JSON.stringify(request)}。优先使用这些信息，只补问影响任务的缺项。` : "先确认希望解决的任务、触发场景和输出要求。"}
${mode === "create" ? "新建前检查同名目录，不要覆盖已有技能。" : "先读取已有 Skill 的正文和所需资源，保留原 ID；目标目录不存在时先用 skill 工具确认实际来源，不要假装已读取。"}
${mode === "review" ? "本次只检查并报告问题与修复建议，不修改文件，不运行目标脚本。" : "使用正常文件工具写入；更新前保留可恢复副本，不覆盖无关文件。不修改启停设置。"}
SKILL.md 使用 YAML front matter，至少包含 name 和 description，正文写清工作流程、验证和错误处理。按需添加 references/、scripts/、templates/、assets/，用相对于技能目录的路径引用。
默认用 $ARGUMENTS 接收整段自然语言输入；需要拆分时支持从零开始的 $0、$1 或 $ARGUMENTS[0]、$ARGUMENTS[1]，引号可将含空格的内容作为一个参数。可选 arguments: [file, focus] 将 $file、$focus 依次映射到位置参数；argument-hint 只提供输入提示。缺少具名参数会展开为空，缺少位置参数会保留占位符；不要因此终止任务，确实需要时再询问。必填、类型和枚举等校验放在工具或脚本中，不要求每个 Skill 声明参数。
支持 \${MDO_SKILL_DIR}；参数按字面替换，不执行 shell 或递归替换。disable-model-invocation: true 表示仅手动选择，user-invocable: false 表示仅模型选择。不要添加未实现的 context/fork/agent/model/hooks 等行为字段。
技能不授予权限，不自动安装依赖，不自动运行导入脚本。用 skill(action: validate, content: ...) 检查草稿；保存后用 name 和 scope: ${scope === "project" ? "project" : "user"} 检查实际目录。校验失败返回 errors 时修正后重检，不中断任务。区分格式检查、资源检查和模型行为试用，不编造已执行的测试。最后说明文件位置、实际验证结果和如何用 $<skill-id> 调用。`;
}

export async function expandSkillAuthoringDraft(settings, options) {
  const args = skillAuthoringPrompt(settings);
  if (new TextEncoder().encode(args).length > 16384) throw new Error("构建请求超过 16 KiB，请缩短需求或路径");
  const response = await api.post(`/skills/invoke?project=${encodeURIComponent(settings.projectId || "default")}`,
    { id: "skill-creator", arguments: args }, options);
  const text = response.data?.text;
  if (typeof text !== "string" || new TextEncoder().encode(text).length > 65536)
    throw new Error("构建器草稿无效或超过 64 KiB；请检查 skill-creator，当前内容已保留");
  return text;
}

// Dedicated conversation: every pre-existing draft and its attachments survives.
export async function createSkillAuthoringDraft({ createSession, draftStore, projectId, text, title = "创建 Skill", check = () => {} }) {
  check();
  const session = await createSession({ project_id: projectId, title });
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
