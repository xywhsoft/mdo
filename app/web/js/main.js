import { boot } from "./app.js";

boot().catch((error) => {
  document.body.textContent = "墨斗界面初始化失败。请重新启动应用。";
  console.error(error);
});
