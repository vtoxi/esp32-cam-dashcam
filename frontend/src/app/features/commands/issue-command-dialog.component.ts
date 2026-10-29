import { Component, Input, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { inject } from '@angular/core';
import type { DialogRef } from '../../shared/components/dialog/dialog.service';
import { DialogShellComponent } from '../../shared/components/dialog/dialog-shell.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { KNOWN_COMMAND_TYPES, SECURITY_MODES } from '../../core/models';
import { CommandService } from '../../core/services/command.service';
import { AdminAuthService } from '../../core/services/admin-auth.service';

/**
 * Issues POST /api/v1/devices/{id}/commands (admin-key gated — Phase 21.8).
 * SECURITY_MODE is the one command type wired end-to-end on the gateway
 * firmware today (backend/README.md); other command types can still be
 * issued here (free-text + raw JSON payload) since the backend itself
 * accepts any commandType string — only the firmware-side dispatch is
 * limited, and that's a firmware concern, not an API contract.
 */
@Component({
  selector: 'app-issue-command-dialog',
  standalone: true,
  imports: [FormsModule, DialogShellComponent, IconComponent],
  templateUrl: './issue-command-dialog.component.html',
})
export class IssueCommandDialogComponent {
  @Input({ required: true }) deviceId = '';
  dialogRef!: DialogRef<boolean>;

  private readonly commandService = inject(CommandService);
  readonly admin = inject(AdminAuthService);

  readonly knownTypes = KNOWN_COMMAND_TYPES;
  readonly securityModes = SECURITY_MODES;

  commandType: string = KNOWN_COMMAND_TYPES[0];
  customType = '';
  securityMode: string = SECURITY_MODES[0];
  payloadDraft = '{}';
  ttlSeconds = 300;

  readonly submitting = signal(false);
  readonly error = signal<string | null>(null);

  get effectiveType(): string {
    return this.commandType === '__custom__' ? this.customType.trim() : this.commandType;
  }

  get payloadPreview(): string {
    if (this.effectiveType === 'SECURITY_MODE') {
      return JSON.stringify({ mode: this.securityMode }, null, 2);
    }
    return this.payloadDraft;
  }

  submit(): void {
    this.error.set(null);
    if (!this.effectiveType) {
      this.error.set('Command type is required.');
      return;
    }
    let payload: unknown;
    try {
      payload = JSON.parse(this.payloadPreview || '{}');
    } catch {
      this.error.set('Payload must be valid JSON.');
      return;
    }
    this.submitting.set(true);
    this.commandService
      .issue(this.deviceId, { commandType: this.effectiveType, payload, ttlSeconds: this.ttlSeconds })
      .subscribe({
        next: () => {
          this.submitting.set(false);
          this.dialogRef.close(true);
        },
        error: () => this.submitting.set(false),
      });
  }
}
