export type CommandStatus = 'PENDING' | 'DELIVERED' | 'EXECUTED' | 'FAILED' | 'EXPIRED' | 'REJECTED';

export interface CommandRecord {
  commandId: string;
  targetDeviceId: string;
  commandType: string;
  payloadJson: string;
  issuedAt: string;
  expiresAt: string;
  status: CommandStatus;
  resultJson: string | null;
  completedAt: string | null;
}

export interface IssueCommandRequest {
  commandType: string;
  payload: unknown;
  ttlSeconds?: number;
}

export interface IssueCommandResponse {
  commandId: string;
  status: CommandStatus;
  expiresAt: string;
}

/** The one command type wired end-to-end on the gateway firmware today (Phase 21.8). */
export const KNOWN_COMMAND_TYPES = ['SECURITY_MODE'] as const;

export const SECURITY_MODES = ['DISARMED', 'DRIVING', 'PARKED', 'SERVICE'] as const;
