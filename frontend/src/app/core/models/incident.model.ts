export interface IncidentRecord {
  incidentId: string;
  deviceId: string;
  state: string;
  firstReceivedAt: string;
  lastReceivedAt: string;
  payloadJson: string;
}

export interface EvidenceRecord {
  id: number;
  incidentId: string;
  nodeId: string;
  eventId: string;
  contentType: string;
  sizeBytes: number;
  uploadedAt: string;
}
