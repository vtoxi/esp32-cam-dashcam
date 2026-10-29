export interface Device {
  id: string;
  hardwareProfile: string;
  firmwareVersion: string;
  nodeId: string;
  tenantId: string | null;
  registeredAt: string;
  lastSeenAt: string;
}

export interface DeviceHealth {
  id: string;
  online: boolean;
  lastSeenAt: string;
}

export interface HealthSummary {
  deviceCount: number;
  onlineCount: number;
  devices: DeviceHealth[];
}
