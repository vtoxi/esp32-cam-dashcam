export interface StreamEnvelope<T = unknown> {
  schemaVersion: number;
  type: string;
  deviceId: string;
  timestamp: string;
  payload: T;
}

export interface ActivityItem {
  id: string;
  type: string;
  deviceId: string;
  timestamp: string;
  payload: unknown;
}
