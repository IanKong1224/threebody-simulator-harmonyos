export interface SimulationStatus {
  running: boolean;
  fps: number;
  presentationFps: number;
  actualDisplayFps: number;
  systemCallbackFps: number;
  refreshRateMessage: string;
  frameGenerationMessage: string;
  simulationDaysPerSecond: number;
  hasRecording: boolean;
  hasFrameRateRecording: boolean;
  frameRateRecordingMessage: string;
  recordingMessage: string;
  accelerationMessage: string;
  gpuMilliseconds: number;
  time: number; // Earth days
  frames: number;
  message: string;
  bodies: number[]; // Repeated mass (solar), position (AU), velocity (km/s).
}
export const start: (gles: boolean, bodies: number[], quality: number, speed: number, planet?: boolean) => boolean;
export const getPreset: (preset: number) => number[];
export const getStatus: () => SimulationStatus;
export const pause: () => boolean;
export const resume: () => boolean;
export const stop: () => boolean;
export const setCamera: (yaw: number, pitch: number, distance: number) => boolean;

export const fitView: () => number;

export const setBrightness: (brightness: number) => boolean;

export const getTrajectory: () => string;

export const isBasic: () => boolean;
export const setTrail: (visible: boolean, duration: number) => boolean;
export const setRecordingDirectory: (directory: string) => boolean;
export const exportFrameRateFile: (csv: boolean) => Promise<string>;
export const exportTrajectoryFile: (csv: boolean) => Promise<string>;

export const getRandomPreset: (seed: number) => number[];

export const isParticles: () => boolean;

export const isAccelerated: () => boolean;
export const setFrameGeneration: (enabled: boolean) => boolean;
export const setAcceleration: (enabled: boolean) => boolean;

export const isPlanet: () => boolean;
export const followPlanet: (enabled: boolean) => number;

export const setOutputWindow: (id: number) => boolean;
