export interface TelemetryRecord {
  id: number;
  deviceId: string;
  receivedAt: string;
  payloadJson: string;
}

export interface EventRecord {
  id: number;
  deviceId: string;
  receivedAt: string;
  payloadJson: string;
}
