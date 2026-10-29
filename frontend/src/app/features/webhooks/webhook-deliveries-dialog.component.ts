import { Component, Input, OnInit, inject, signal } from '@angular/core';
import type { DialogRef } from '../../shared/components/dialog/dialog.service';
import { DialogShellComponent } from '../../shared/components/dialog/dialog-shell.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { EmptyStateComponent } from '../../shared/components/empty-state/empty-state.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { WebhookDelivery } from '../../core/models';
import { WebhookService } from '../../core/services/webhook.service';

@Component({
  selector: 'app-webhook-deliveries-dialog',
  standalone: true,
  imports: [DialogShellComponent, StatusBadgeComponent, EmptyStateComponent, RelativeTimePipe],
  templateUrl: './webhook-deliveries-dialog.component.html',
})
export class WebhookDeliveriesDialogComponent implements OnInit {
  @Input({ required: true }) subscriptionId!: number;
  @Input() url = '';
  dialogRef!: DialogRef<void>;

  private readonly webhookService = inject(WebhookService);
  readonly loading = signal(true);
  readonly deliveries = signal<WebhookDelivery[]>([]);

  ngOnInit(): void {
    this.webhookService.deliveries(this.subscriptionId, 50).subscribe({
      next: (rows) => {
        this.deliveries.set(rows);
        this.loading.set(false);
      },
      error: () => this.loading.set(false),
    });
  }
}
