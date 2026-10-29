import { Component, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import type { DialogRef } from '../../shared/components/dialog/dialog.service';
import { DialogShellComponent } from '../../shared/components/dialog/dialog-shell.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { CopyFieldComponent } from '../../shared/components/copy-field/copy-field.component';
import { KNOWN_EVENT_TYPES, WebhookSubscriptionCreated } from '../../core/models';
import { WebhookService } from '../../core/services/webhook.service';

/** Creates a webhook subscription (POST /api/v1/webhooks, admin-key gated —
 * Phase 21.9) and shows the generated HMAC secret exactly once, matching what
 * the backend itself does (never returned again after this response). */
@Component({
  selector: 'app-create-webhook-dialog',
  standalone: true,
  imports: [FormsModule, DialogShellComponent, IconComponent, CopyFieldComponent],
  templateUrl: './create-webhook-dialog.component.html',
})
export class CreateWebhookDialogComponent {
  dialogRef!: DialogRef<boolean>;

  private readonly webhookService = inject(WebhookService);
  readonly knownEventTypes = KNOWN_EVENT_TYPES;

  url = '';
  selectedTypes = new Set<string>();
  allEvents = true;

  readonly submitting = signal(false);
  readonly error = signal<string | null>(null);
  readonly created = signal<WebhookSubscriptionCreated | null>(null);

  toggleType(type: string): void {
    if (this.selectedTypes.has(type)) this.selectedTypes.delete(type);
    else this.selectedTypes.add(type);
  }

  submit(): void {
    this.error.set(null);
    if (!this.url.trim()) {
      this.error.set('A URL is required.');
      return;
    }
    const eventTypes = this.allEvents || this.selectedTypes.size === 0 ? '*' : [...this.selectedTypes].join(',');
    this.submitting.set(true);
    this.webhookService.create({ url: this.url.trim(), eventTypes }).subscribe({
      next: (result) => {
        this.submitting.set(false);
        this.created.set(result);
      },
      error: () => this.submitting.set(false),
    });
  }

  done(): void {
    this.dialogRef.close(true);
  }
}
