import { webview } from '@kit.ArkWeb';
import { hilog } from '@kit.PerformanceAnalysisKit';
import nativeProbe from 'libmdoprobe.so';

class ProbeBridge {
  aotValue(): number { return nativeProbe.aotValue(); }
  port(): number { return nativeProbe.startServer(); }
}

@Entry
@Component
struct Index {
  @State nativeValue: number = 0;
  @State tccResult: string = '未执行';
  @State platformResult: string = '';
  private controller: webview.WebviewController = new webview.WebviewController();
  private bridge: ProbeBridge = new ProbeBridge();

  aboutToAppear(): void {
    this.nativeValue = nativeProbe.aotValue();
    hilog.info(0, 'MdoProbe', 'aot_value=%{public}d', this.nativeValue);
    this.platformResult = `私有文件 errno=${AppStorage.get<number>('fileResult')}；系统 shell errno=${AppStorage.get<number>('spawnResult')}；Native 子进程结果=${AppStorage.get<number>('childResult')}`;
    if (AppStorage.get<boolean>('probeTcc') === true) this.runTcc();
  }
  private runTcc(): void {
    this.tccResult = nativeProbe.runTcc() === 0 ? '通过' : '失败';
    hilog.info(0, 'MdoProbe', 'tcc_result=%{public}s', this.tccResult);
  }
  build() {
    Column({space: 12}) {
      Text('mdo 原生鸿蒙平台探测').fontSize(24)
      Text(`预编译 C 函数：${this.nativeValue}（预期 42）`)
      Text(`TCC 动态执行：${this.tccResult}`)
      Text(this.platformResult).fontSize(14)
      Button('运行 TCC 探测').onClick(() => { this.runTcc(); })
      Web({src: $rawfile('probe.html'), controller: this.controller})
        .javaScriptAccess(true)
        .javaScriptProxy({object: this.bridge, name: 'nativeProbe', methodList: ['aotValue', 'port'], controller: this.controller})
        .onConsole((event) => {
          if (event) hilog.info(0, 'MdoProbe', 'web=%{public}s', event.message.getMessage());
          return false;
        })
        .width('100%').layoutWeight(1)
    }.padding(20).width('100%').height('100%')
  }
}
