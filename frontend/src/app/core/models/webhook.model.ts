export interface WebhookSubscription {
  id: number;
  url: string;
  eventTypes: string;
  enabled: boolean;
  createdAt: string;
}

export interface WebhookSubscriptionCreated extends WebhookSubscription {
  secret: string;
}

export interface WebhookDelivery {
  id: number;
  subscriptionId: number;
  eventType: string;
  success: boolean;
  statusCode: number | null;
  attemptedAt: string;
}

export interface CreateWebhookRequest {
  url: string;
  eventTypes?: string;
}

/** Mirrors the taxonomy EventBroadcaster publishes (docs/REMOTE_ACCESS.md Section 5). */
export const KNOWN_EVENT_TYPES = [
  'device.online',
  'device.heartbeat',
  'telemetry',
  'motion.detected',
  'incident.created',
  'incident.updated',
  'evidence.uploaded',
  'command.issued',
  'command.executed',
  'command.failed',
] as const;
