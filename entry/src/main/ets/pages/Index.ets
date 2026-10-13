import { fileIo as fs, picker } from '@kit.CoreFileKit';
import { common } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import simulator, { SimulationStatus } from 'libentry.so';

@Entry
@Component
struct Index {
  private planetEdition: boolean = simulator.isPlanet();
  @State planetEnabled: boolean = true;
  @State followingPlanet: boolean = false;
  private acceleratedEdition: boolean = simulator.isAccelerated();
  @State gpuMilliseconds: number = -1;
  @State acceleration: boolean = false;
  @State frameGeneration: boolean = false;
  @State presentationFps: number = 0;
  @State actualDisplayFps: number = -1;
  @State systemCallbackFps: number = -1;
  @State refreshRateMessage: string = "";
  @State frameGenerationMessage: string = "超帧关闭";
  @State accelerationMessage: string = '开始模拟后检测加速状态';
  private basic: boolean = simulator.isBasic();
  private particles: boolean = simulator.isParticles();
  @State hasRecording: boolean = false;
  @State hasFrameRateRecording: boolean = false;
  @State frameRateRecordingMessage: string = "";
  @State recordingMessage: string = '';
  @State simulationRate: number = 0;
  @State showTrails: boolean = true;
  @State trailDuration: number = 12;
  @State panelProgress: number = 1;
  @State panelHeight: number = 600;
  private panelDragStart: number = 1;

  private setPanel(open: boolean): void {
    this.getUIContext().animateTo({ duration: 240, curve: Curve.EaseOut }, () => {
      this.panelProgress = open ? 1 : 0;
    });
  }
  private finishPanelDrag(velocity: number): void {
    this.setPanel(Math.abs(velocity) > 500 ? velocity < 0 : this.panelProgress >= 0.5);
  }
  private collapseWhenReady: boolean = false;
  @State exportBusy: boolean = false;
  @State exportMessage: string = '';
  @State running: boolean = false;
  @State started: boolean = false;
  @State fps: number = 0;
  @State time: number = 0;
  @State message: string = '选择初始状态，然后开始模拟';
  @State error: string = '';
  @State preset: number = 0;
  private randomFields: string[] = [];
  @State star: number = 0;
  @State fields: string[] = [];
  @State bodies: number[] = [];
  @State quality: number = 1;
  @State speed: number = 1;
  @State stellarBrightness: number = 70;
  @State gles: boolean = false;
  @State yaw: number = -1.5707963;
  @State pitch: number = .70;
  @State distance: number = 5;
  private panYaw: number = 0;
  private panPitch: number = 0;
  private pinchDistance: number = 5;
  private timer: number = -1;
  private displayWindow: window.Window | undefined = undefined;
  private pageVisible: boolean = true;
  private applyScreenAwake(): void {
    if (this.displayWindow) {
      this.displayWindow.setWindowKeepScreenOn(this.pageVisible && this.running)
        .catch(() => { this.error = '保持亮屏设置失败，请检查系统息屏设置'; });
    }
  }
  private names: string[] = ['恒星一 · 橙色', '恒星二 · 蓝色', '恒星三 · 金色'];
  private labels: string[] = ['质量 M☉', 'X · AU', 'Y · AU', 'Z · AU', 'Vx · km/s', 'Vy · km/s', 'Vz · km/s'];

  aboutToAppear(): void {
    this.pageVisible = true;
    const context = this.getUIContext().getHostContext();
    if (context) {
      window.getLastWindow(context).then((w: window.Window) => {
        this.displayWindow = w;
        this.applyScreenAwake();
      }).catch(() => { this.error = '无法获取窗口，请检查系统息屏设置'; });
    }
    this.selectPreset(this.planetEdition ? 5 : 0);
    try {
      const appContext = this.getUIContext().getHostContext() as common.UIAbilityContext;
      const directory = appContext.filesDir + '/trajectories';
      if (!fs.accessSync(directory)) fs.mkdirSync(directory, true);
      simulator.setRecordingDirectory(directory);
    } catch (e) { this.error = '轨迹保存初始化失败：' + (e as Error).message; }
    simulator.setBrightness(this.stellarBrightness / 70);
    this.timer = setInterval(() => this.refresh(), 250);
  }
  aboutToDisappear(): void { this.pageVisible = false; this.applyScreenAwake(); if (this.timer >= 0) clearInterval(this.timer); simulator.stop(); }
  onBackPress(): boolean {
    if (this.panelProgress > 0) { this.setPanel(false); return true; }
    return false;
  }
  onPageHide(): void { this.pageVisible = false; this.applyScreenAwake(); simulator.pause(); this.refresh(); }
  onPageShow(): void { this.pageVisible = true; this.applyScreenAwake(); }
  private refresh(): void {
    const s: SimulationStatus = simulator.getStatus();
    this.accelerationMessage = s.accelerationMessage;
    this.presentationFps = s.presentationFps;
    this.actualDisplayFps = s.actualDisplayFps;
    this.systemCallbackFps = s.systemCallbackFps;
    this.refreshRateMessage = s.refreshRateMessage;
    this.frameGenerationMessage = s.frameGenerationMessage;
    this.gpuMilliseconds = s.gpuMilliseconds;
    if (this.collapseWhenReady && s.running && s.frames > 0) {
      this.setPanel(false); this.collapseWhenReady = false;
    }
    if (this.collapseWhenReady && !s.running) {
      this.setPanel(true); this.collapseWhenReady = false;
    }
    if (this.running && !s.running) this.setPanel(true);
    const runningChanged = this.running !== s.running;
    this.running = s.running;
    if (runningChanged) this.applyScreenAwake();
    this.fps = s.fps; this.time = s.time;
    this.hasFrameRateRecording = s.hasFrameRateRecording;
    this.frameRateRecordingMessage = s.frameRateRecordingMessage;
    this.hasRecording = s.hasRecording; this.recordingMessage = s.recordingMessage; this.simulationRate = s.simulationDaysPerSecond;
    this.message = s.message; this.bodies = s.bodies;
    if (s.frames > 0) this.started = true;
  }
  private selectPreset(id: number): void {
    this.preset = id;
    if (id === 0) {
      if (this.randomFields.length !== 21) { this.randomize(); return; }
      this.fields = this.randomFields.slice();
    } else {
      this.fields = simulator.getPreset(id).map((value: number) => value.toString());
    }
    this.error = '';
  }
  private randomize(): void {
    if (this.running || this.exportBusy) return;
    const seed: number = Math.floor(Math.random() * 4294967296);
    this.randomFields = simulator.getRandomPreset(seed).map((value: number) => value.toString());
    this.fields = this.randomFields.slice(); this.preset = 0; this.error = '';
  }
  private edit(index: number, value: string): void {
    const next: string[] = this.fields.slice(); next[index] = value;
    this.fields = next; this.preset = 2;
  }
  private start(): void {
    this.error = '';
    if (this.fields.length !== 21 || this.fields.some((v: string) => v.trim().length === 0 || !Number.isFinite(Number(v)))) {
      this.error = '请完整填写三颗恒星的有效数字'; return;
    }
    try {
      this.camera();
      simulator.setTrail(this.showTrails, this.trailDuration);
      if (!simulator.start(this.gles, this.fields.map((v: string) => Number(v)), this.quality, this.speed, this.planetEdition && this.planetEnabled)) {
        this.error = '画面尚未准备好，请稍后重试'; return;
      }
      this.followingPlanet = false;
      this.collapseWhenReady = true;
      this.started = true; this.distance = simulator.fitView(); this.refresh();
    } catch (e) { this.error = (e as Error).message; }
  }
  private async exportTrajectory(csv: boolean, frameRate: boolean = false): Promise<void> {
    this.exportBusy = true; this.exportMessage = '';
    try {
      simulator.pause(); this.refresh();
      const source = frameRate ? await simulator.exportFrameRateFile(true) : await simulator.exportTrajectoryFile(csv);
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      const options = new picker.DocumentSaveOptions();
      const suffix = csv ? 'csv' : 'json';
      options.newFileNames = ['threebody-' + (frameRate ? 'fps-' : '') + (this.planetEdition ? 'planet-' : this.basic ? 'basic-' : this.particles ? 'particles-' : this.acceleratedEdition ? 'accelerated-' : 'detailed-') + Date.now().toString() + '.' + suffix];
      options.fileSuffixChoices = [csv ? 'CSV|.csv' : 'JSON|.json'];
      const paths = await new picker.DocumentViewPicker(context).save(options);
      if (paths.length === 0) { this.exportMessage = '已取消导出，记录仍保存在应用内'; return; }
      const input = fs.openSync(source, fs.OpenMode.READ_ONLY);
      try {
        const output = fs.openSync(paths[0], fs.OpenMode.WRITE_ONLY | fs.OpenMode.CREATE | fs.OpenMode.TRUNC);
        try {
          const buffer = new ArrayBuffer(65536);
          while (true) {
            const count = await fs.read(input.fd, buffer);
            if (count === 0) break;
            let offset = 0;
            while (offset < count) {
              const written = await fs.write(output.fd, buffer.slice(offset, count));
              if (written <= 0) throw new Error('文件未写完');
              offset += written;
            }
          }
          fs.fsyncSync(output.fd);
        } finally { fs.closeSync(output); }
      } finally { fs.closeSync(input); }
      this.exportMessage = frameRate ? '帧率 CSV 已导出，可查看随时间的变化' : '轨迹已导出；JSON 保留完整参数，CSV 便于查看';
    } catch (e) { this.exportMessage = '导出失败：' + (e as Error).message; }
    finally { this.exportBusy = false; }
  }
  private camera(): void { simulator.setCamera(this.yaw, this.pitch, this.distance); }
  private current(offset: number): string {
    if (!this.started || this.bodies.length !== 21) return '—';
    return this.bodies[this.star * 7 + offset].toFixed(4);
  }
  build() {
    Stack({ alignContent: Alignment.Bottom }) {
          XComponent({ id: 'threebody_surface', type: XComponentType.SURFACE, libraryname: 'entry' })
            .width('100%').height('100%')
            .expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM, SafeAreaEdge.START, SafeAreaEdge.END])
            .gesture(GestureGroup(GestureMode.Parallel,
              PanGesture({ fingers: 1 })
                .onActionStart(() => { this.panYaw = this.yaw; this.panPitch = this.pitch; })
                .onActionUpdate((event: GestureEvent) => {
                  this.yaw = this.panYaw + event.offsetX * .008;
                  this.pitch = Math.max(-1.35, Math.min(1.35, this.panPitch + event.offsetY * .008)); this.camera();
                }),
              PinchGesture({ fingers: 2 })
                .onActionStart(() => { this.pinchDistance = this.distance; })
                .onActionUpdate((event: GestureEvent) => {
                  this.distance = Math.max(this.followingPlanet ? .18 : 2, Math.min(500, this.pinchDistance / Math.max(.1, event.scale))); this.camera();
                })
            ))

      Column() {
        // Equal side columns keep the handle centered regardless of the FPS text width.
        Row() {
          Column({ space: 2 }) {
            Text(this.running ? (this.actualDisplayFps >= 0 ? `显示 ${this.actualDisplayFps.toFixed(1)} FPS` : `渲染 ${this.fps.toFixed(1)} FPS`) : (this.started ? '已暂停' : '三体模拟器'))
              .fontSize(14).fontColor('#C9D5E8').maxLines(1)
              .textOverflow({ overflow: TextOverflow.Ellipsis })
            if (this.running && this.acceleratedEdition && this.frameGeneration) {
              Text(`含合成 ${this.presentationFps.toFixed(1)} FPS`)
                .fontSize(12).fontColor('#AAB7CB').maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .accessibilityText(`含合成帧提交速率 ${this.presentationFps.toFixed(1)} FPS，不代表实际屏幕帧率`)
            }
          }.alignItems(HorizontalAlign.Start).layoutWeight(1).padding({ left: 16, right: 8 })
          Rect().width(40).height(4).radius(2).fill('#CCD5E3')
          Row().layoutWeight(1)
        }.width('100%').height(44).flexShrink(0).alignItems(VerticalAlign.Center)
          .accessibilityText(this.panelProgress > 0.5 ? '下滑收起控制菜单' : '上滑展开控制菜单')
          .onClick(() => { this.collapseWhenReady = false; this.setPanel(this.panelProgress < 0.5); })
          .gesture(
            PanGesture({ direction: PanDirection.Vertical, distance: 4 })
              .onActionStart(() => {
                this.collapseWhenReady = false;
                this.panelDragStart = this.panelProgress;
              })
              .onActionUpdate((event: GestureEvent) => {
                this.panelProgress = Math.max(0, Math.min(1,
                  this.panelDragStart - event.offsetY / Math.max(1, this.panelHeight - 44)));
              })
              .onActionEnd((event: GestureEvent) => this.finishPanelDrag(event.velocityY))
              .onActionCancel(() => this.finishPanelDrag(0))
          )
        Column() {
          Row() {
            Column({ space: 3 }) {
              Text(this.planetEdition ? '三体模拟器 · 行星实验版' : this.basic ? '三体模拟器' : this.particles ? '三体模拟器Pro' : this.acceleratedEdition ? '三体模拟器Pro Max' : '三体模拟器 · 光影版').fontSize(20).fontWeight(FontWeight.Bold).fontColor('#F4F7FC')
              Text(this.basic ? '点状恒星 · 线条轨迹 · RK4' : this.particles ? '球体恒星 · 粒子轨迹 · RK4' : '恒星细节 · HDR 光晕 · RK45').fontSize(12).fontColor('#AAB7CB')
            }.alignItems(HorizontalAlign.Start).layoutWeight(1)
          }.padding(16).width('100%')
          Scroll() {
            Column({ space: 12 }) {
        Row({ space: 10 }) {
          Button(this.running ? '暂停' : (this.started ? '继续' : '开始模拟'))
            .enabled(!this.exportBusy).onClick(() => {
              if (this.running) simulator.pause();
              else if (this.started) { if (!simulator.resume()) this.error = '请重新开始模拟'; else this.collapseWhenReady = true; }
              else this.start();
              this.refresh();
            })
          Button('重新开始').enabled(!this.exportBusy).onClick(() => this.start())
          Button('复位视角').onClick(() => { this.followingPlanet = false; this.yaw = -1.5707963; this.pitch = .70; this.distance = simulator.fitView(); this.camera(); })
        }.width('100%')
        Text(`已模拟 ${this.time.toFixed(2)} 地球天（${(this.time / 365.25).toFixed(2)} 年） · ${this.message}`).fontSize(13).fontColor('#E6EDF7').width('100%')
        if (this.planetEdition) {
          Row({ space: 12 }) {
            Text('加入行星（重新开始后生效）').fontColor('#E6EDF7').layoutWeight(1)
            Toggle({ type: ToggleType.Switch, isOn: this.planetEnabled }).enabled(!this.running)
              .onChange((value: boolean) => { this.planetEnabled = value; })
          }.width('100%')
          Button(this.followingPlanet ? '查看整个系统' : '跟随行星').enabled(this.started && this.planetEnabled)
            .onClick(() => {
              this.followingPlanet = !this.followingPlanet;
              this.distance = this.followingPlanet ? simulator.followPlanet(true) : simulator.fitView();
              this.camera();
            })
          Text('绿色轨迹为行星。地球半径；显示半径放大约 1644 倍。初始距恒星一 0.35 AU，速度为单星圆轨道估计；会受另外两颗恒星扰动，不保证稳定或宜居。')
            .fontSize(12).fontColor('#9AA9BF').width('100%')
          Text('实验模型：行星受三颗恒星引力影响，忽略反作用。独立自适应积分，接触前暂停。岩石表面接受三光源照明；暂无光追、大气与海洋。导出包含行星真实位置与速度。')
            .fontSize(12).fontColor('#9AA9BF').width('100%')
        }
        if (this.error.length > 0) { Text(this.error).fontColor('#FF9C91').fontSize(14).width('100%') }
        Text('初始状态').fontSize(20).fontWeight(FontWeight.Medium).fontColor('#E6EDF7').width('100%')
        Row({ space: 8 }) {
          Button('稳定立体三体').fontSize(14).layoutWeight(1).height(40).enabled(!this.running)
            .backgroundColor(this.preset === 4 ? '#175CFF' : '#34415A')
            .onClick(() => this.selectPreset(4))
          Button('长时立体三体').fontSize(14).layoutWeight(1).height(40).enabled(!this.running)
            .backgroundColor(this.preset === 3 ? '#175CFF' : '#34415A')
            .onClick(() => this.selectPreset(3))
        }.width('100%')
        Row({ space: 8 }) {
          Button('随机立体三体').fontSize(14).layoutWeight(1).height(40).enabled(!this.running)
            .backgroundColor(this.preset === 0 ? '#175CFF' : '#34415A')
            .onClick(() => this.selectPreset(0))
          Button('八字模型').fontSize(14).layoutWeight(1).height(40).enabled(!this.running)
            .backgroundColor(this.preset === 1 ? '#175CFF' : '#34415A')
            .onClick(() => this.selectPreset(1))
        }.width('100%')
        Row({ space: 8 }) {
          Button('三日交错').fontSize(14).layoutWeight(1).height(40).enabled(!this.running)
            .backgroundColor(this.preset === 5 ? '#175CFF' : '#34415A')
            .onClick(() => this.selectPreset(5))
          Blank().layoutWeight(1)
        }.width('100%')
        if (this.preset === 5) {
          Text('三颗太阳交错、远去后返回；已验证约 12.7 地球年，实时计算，不循环回放。之后仍可能接触或逃逸。').fontSize(12).fontColor('#9AA9BF').width('100%')
        } else if (this.preset === 4) {
          Text('双星 + 倾斜外轨道；已验证约 100 地球年，无永久稳定保证。').fontSize(12).fontColor('#9AA9BF').width('100%')
        } else if (this.preset === 3) {
          Text('非分层立体运动；已验证约 63.7 地球年无接触、无逃逸迹象，不保证永久安全。').fontSize(12).fontColor('#9AA9BF').width('100%')
        } else if (this.preset === 0) {
          Button('重新随机').enabled(!this.running && !this.exportBusy).onClick(() => this.randomize())
          Text('每颗质量固定 1 M☉，初始速度固定为八字模型的速度；仅随机三维位置，初始间距至少 0.5 AU。仍可能接触或逃逸。').fontSize(12).fontColor('#9AA9BF').width('100%')
        } else if (this.preset === 1) {
          Text('等质量、近周期八字轨道，位于同一平面。').fontSize(12).fontColor('#9AA9BF').width('100%')
        } else {
          Text('自定义参数：不再适用预设验证结果。').fontSize(12).fontColor('#9AA9BF').width('100%')
        }
        Text('暂停后编辑参数；点击“重新开始”应用。“继续”保持原来的状态与设置。').fontSize(12).fontColor('#9AA9BF').width('100%')
        Text('模拟速度').fontSize(17).fontColor('#E6EDF7').width('100%')
        Row({ space: 8 }) {
          ForEach([0.25, 1, 4], (v: number) => {
            Button(`${v}×`).enabled(!this.running).backgroundColor(this.speed === v ? '#175CFF' : '#34415A').onClick(() => { this.speed = v; })
          }, (v: number) => v.toString())
        }
        Text('1×：每现实秒目标推进约 58.13 地球天；0.25× 约 14.53 天，4× 约 232.53 天。负载过高时会放慢，以已模拟天数为准。').fontSize(12).fontColor('#9AA9BF').width('100%')
        Text(`实际推进：${this.simulationRate.toFixed(2)} 模拟天 / 现实秒`).fontSize(12).fontColor('#9AA9BF').width('100%')
        Text('恒星参数').fontSize(20).fontWeight(FontWeight.Medium).fontColor('#E6EDF7').width('100%').margin({ top: 12 })
        Row({ space: 8 }) {
          ForEach([0, 1, 2], (i: number) => {
            Button(`恒星 ${i + 1}`).backgroundColor(this.star === i ? '#175CFF' : '#34415A').onClick(() => { this.star = i; })
          }, (i: number) => i.toString())
        }.width('100%')
        Text(this.names[this.star]).fontSize(16).fontColor('#E6EDF7').width('100%')
        ForEach([0, 1, 2, 3, 4, 5, 6], (i: number) => {
          Row({ space: 12 }) {
            Text(this.labels[i]).fontColor('#E6EDF7').width(70).fontSize(14)
            TextInput({ text: this.fields[this.star * 7 + i] ?? '', placeholder: this.labels[i] })
              .layoutWeight(1).fontColor('#E6EDF7').backgroundColor('#283346').enabled(!this.running)
              .onChange((value: string) => this.edit(this.star * 7 + i, value))
          }.width('100%')
        }, (i: number) => i.toString())
        if (this.started) {
          Column({ space: 5 }) {
            Text('当前计算状态').fontWeight(FontWeight.Medium).fontColor('#E6EDF7').width('100%')
            Text(`质量 ${this.current(0)} M☉`).fontColor('#E6EDF7').width('100%')
            Text(`物理半径 ${(Math.pow(this.bodies[this.star * 7] ?? 1, 0.8) * 695700).toFixed(0)} 公里（简化估计）`).fontColor('#E6EDF7').width('100%')
            Text(`位置 (${this.current(1)}, ${this.current(2)}, ${this.current(3)}) AU`).fontColor('#E6EDF7').width('100%')
            Text(`速度 (${this.current(4)}, ${this.current(5)}, ${this.current(6)}) km/s`).fontColor('#E6EDF7').width('100%')
          }.padding(12).backgroundColor('#283346').borderRadius(10)
        }
        Text('画面设置').fontSize(20).fontWeight(FontWeight.Medium).fontColor('#E6EDF7').width('100%').margin({ top: 12 })
        if (!this.basic && !this.particles) {
        Text(`恒星亮度 · ${this.stellarBrightness.toFixed(0)}%`).fontSize(17).fontColor('#E6EDF7').width('100%')
        Slider({ value: this.stellarBrightness, min: 0, max: 100, step: 1 })
          .width('100%').selectedColor('#85B6FF').trackColor('#46546B')
          .onChange((value: number) => {
            this.stellarBrightness = value;
            simulator.setBrightness(value / 70);
          })
        Text('低：看表面细节 · 高：耀眼光晕；运行中可直接调整')
          .fontSize(12).fontColor('#9AA9BF').width('100%')
        }
        Row({ space: 12 }) {
          Text('显示轨迹').fontColor('#E6EDF7').layoutWeight(1)
          Toggle({ type: ToggleType.Switch, isOn: this.showTrails })
            .onChange((value: boolean) => { this.showTrails = value; simulator.setTrail(value, this.trailDuration); })
        }.width('100%')
        Text('轨迹长度').fontColor('#E6EDF7').width('100%')
        Row({ space: 8 }) {
          ForEach([6, 12, 18], (duration: number) => {
            Button(duration === 6 ? '短' : duration === 12 ? '中' : '长')
              .backgroundColor(this.trailDuration === duration ? '#175CFF' : '#34415A')
              .onClick(() => { this.trailDuration = duration; simulator.setTrail(this.showTrails, duration); })
          }, (duration: number) => duration.toString())
        }
        if (!this.basic) {
        Text('画面质量').fontSize(17).fontColor('#E6EDF7').width('100%')
        Row({ space: 8 }) {
          ForEach([0, 1, 2], (q: number) => {
            Button(q === 0 ? '标准' : q === 1 ? '精细' : '高质量').enabled(!this.running)
              .backgroundColor(this.quality === q ? '#175CFF' : '#34415A').onClick(() => { this.quality = q; })
          }, (q: number) => q.toString())
        }
        }
        Text('渲染与加速').fontSize(20).fontWeight(FontWeight.Medium).fontColor('#E6EDF7').width('100%').margin({ top: 12 })
        Row({ space: 8 }) {
          Button('Vulkan').enabled(!this.running).backgroundColor(!this.gles ? '#175CFF' : '#34415A').onClick(() => { this.gles = false; })
          Button('OpenGL ES').enabled(!this.running).backgroundColor(this.gles ? '#175CFF' : '#34415A').onClick(() => { this.gles = true; })
        }
        if (this.acceleratedEdition) {
          Row({ space: 12 }) {
            Text('XEngine 渲染加速').fontColor('#E6EDF7').layoutWeight(1)
            Toggle({ type: ToggleType.Switch, isOn: this.acceleration })
              .onChange((value: boolean) => {
                this.acceleration = value;
                if (!simulator.setAcceleration(value)) this.error = '加速切换失败，请重新开始模拟';
                this.refresh();
              })
          }.width('100%')
          Text(this.gles ? 'OpenGL ES 使用原生渲染；切换 Vulkan 可使用超分' : this.accelerationMessage)
            .fontSize(12).fontColor('#9AA9BF').width('100%')
          Text(this.gpuMilliseconds >= 0 ? `GPU 绘制含超分/超帧：${this.gpuMilliseconds.toFixed(2)} ms（不含送显）` : 'GPU 计时暂不可用')
            .fontSize(12).fontColor('#9AA9BF').width('100%')
          Text('超分：75% 内部分辨率重建至全屏；关闭则使用原生画质。')
            .fontSize(12).fontColor('#9AA9BF').width('100%')
          Row({ space: 12 }) {
            Text('Graphics 超帧（实验）').fontColor('#E6EDF7').layoutWeight(1)
            Toggle({ type: ToggleType.Switch, isOn: this.frameGeneration })
              .onChange((value: boolean) => {
                this.frameGeneration = value;
                if (!simulator.setFrameGeneration(value)) this.error = '超帧切换失败，请重新开始模拟';
                this.refresh();
              })
          }.width('100%')
          Text(this.gles ? '超帧仅支持 Vulkan；当前使用真实渲染' : this.frameGenerationMessage)
            .fontSize(12).fontColor('#9AA9BF').width('100%')
          Text(`真实渲染 ${this.fps.toFixed(1)} FPS · 含生成帧提交 ${this.presentationFps.toFixed(1)} FPS`)
            .fontSize(12).fontColor('#9AA9BF').width('100%')
          Text('超帧生效：60 真实帧目标＋中间帧，目标 120。送显统计不代表屏幕实际刷新；底部仅在取得实际上屏时间戳时显示“显示 FPS”，否则标注“渲染 FPS”。粒子可能有拖影。物理计算与导出不变。')
            .fontSize(12).fontColor('#9AA9BF').width('100%')
        }
        Text(this.actualDisplayFps >= 0 ? `实际上屏 ${this.actualDisplayFps.toFixed(1)} FPS` : '实际上屏 FPS 暂不可用；底部显示渲染速率，不能当作屏幕帧率')
          .fontSize(12).fontColor('#9AA9BF').width('100%')
        Text(`系统请求 ${this.basic ? 60 : 120} Hz · 回调 ${this.systemCallbackFps >= 0 ? this.systemCallbackFps.toFixed(1) : '待测'} Hz（回调不是上屏帧数）`)
          .fontSize(12).fontColor('#9AA9BF').width('100%')
        Text(this.refreshRateMessage).fontSize(12).fontColor('#9AA9BF').width('100%')
        Text('记录与导出').fontSize(20).fontWeight(FontWeight.Medium).fontColor('#E6EDF7').width('100%').margin({ top: 12 })
        Row({ space: 8 }) {
          Button(this.exportBusy ? '正在导出…' : '轨迹 JSON')
            .enabled(this.hasRecording && !this.exportBusy).onClick(() => this.exportTrajectory(false))
          Button('轨迹 CSV').enabled(this.hasRecording && !this.exportBusy).onClick(() => this.exportTrajectory(true))
        }
        if (this.basic || this.particles || this.acceleratedEdition) {
          Button('导出帧率 CSV').enabled(this.hasFrameRateRecording && !this.exportBusy)
            .onClick(() => this.exportTrajectory(true, true))
          Text(this.frameRateRecordingMessage).fontSize(12).fontColor('#9AA9BF').width('100%')
          Text('自动每半秒记录提交速率和渲染速率；实际上屏 FPS 无法测量时留空。暂停区间也保留；重新开始会新建记录，退出后仍可导出最近一次记录。')
            .fontSize(12).fontColor('#9AA9BF').width('100%')
        }
        Text(this.recordingMessage).fontSize(12).fontColor('#9AA9BF').width('100%')
        Text('按固定模拟时间自动记录原始位置与速度，独立于 FPS；退出后保留。导出最近一次模拟，重新开始会建立新记录。记录总量上限 256 MiB，达到上限仅停止记录。')
          .fontSize(12).fontColor('#9AA9BF').width('100%')
        if (this.exportMessage.length > 0) { Text(this.exportMessage).fontSize(13).fontColor('#E6EDF7').width('100%') }
        Text('模型说明').fontSize(20).fontWeight(FontWeight.Medium).fontColor('#E6EDF7').width('100%').margin({ top: 12 })
        Text('建模假设 · 小说启发，非原著官方参数或真实观测系统。三颗类太阳主序星，质量范围 0.8～1.2 M☉；半径近似为质量的 0.8 次方（单位 R☉）。1 AU = 约 1.496 亿公里，1 R☉ = 695700 公里。球对称恒星按牛顿引力运动，表面接触时暂停；暂不模拟潮汐、碰撞和恒星演化。')
          .fontSize(12).fontColor('#9AA9BF').width('100%')
        Text(this.basic ? '圆点只是质量中心的位置标记，不代表实际半径；与后续版本使用相同引力、质量和接触规则。' : this.particles ? '球体大小为放大示意，粒子是轨迹的视觉表现，不参与引力计算。' : '恒星与光晕为放大示意，颜色用于区分恒星；画面大小、亮度滑块不改变物理半径或引力。').fontSize(12).fontColor('#9AA9BF').width('100%')
        Text(`持续模拟，无时长上限；运行时保持亮屏。真实渲染默认目标 ${this.basic ? 60 : 120} FPS（超帧生效时为 60），实际取决于设备与系统。“显示 FPS”来自上屏时间戳；“渲染 FPS”仅为渲染完成速率。物理推进速度独立于渲染帧率。`).fontSize(12).fontColor('#9AA9BF').width('100%')
            }.padding({ left: 16, right: 16, bottom: 24 }).width('100%')
          }.layoutWeight(1).width('100%').scrollBar(BarState.Auto)
        }.width('100%').height(this.panelHeight - 44).flexShrink(0)
          .opacity(Math.min(1, this.panelProgress * 3))
          .hitTestBehavior(this.panelProgress === 0 ? HitTestMode.None : HitTestMode.Default)
      }.width('94%').constraintSize({ maxWidth: 480 })
        .height(44 + (this.panelHeight - 44) * this.panelProgress)
        .backgroundColor(`rgba(27, 34, 48, ${0.96 * this.panelProgress})`)
        .borderRadius(24).clip(true)
        .margin({ bottom: 28 })
    }.width('100%').height('100%').backgroundColor(Color.Black)
      .onAreaChange((_oldArea: Area, newArea: Area) => {
        this.panelHeight = Math.max(160, Number(newArea.height) * 0.78);
      })
      .expandSafeArea([SafeAreaType.SYSTEM], [SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM, SafeAreaEdge.START, SafeAreaEdge.END])
  }
}
