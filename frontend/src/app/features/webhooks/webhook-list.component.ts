import { Component, OnInit, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { EmptyStateComponent } from '../../shared/components/empty-state/empty-state.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { WebhookSubscription } from '../../core/models';
import { WebhookService } from '../../core/services/webhook.service';
import { AdminAuthService } from '../../core/services/admin-auth.service';
import { DialogService } from '../../shared/components/dialog/dialog.service';
import { NotificationService } from '../../core/services/notification.service';
import { CreateWebhookDialogComponent } from './create-webhook-dialog.component';
import { WebhookDeliveriesDialogComponent } from './webhook-deliveries-dialog.component';

@Component({
  selector: 'app-webhook-list',
  standalone: true,
  imports: [
    RouterLink,
    DataTableComponent,
    CellTemplateDirective,
    PageHeaderComponent,
    StatusBadgeComponent,
    EmptyStateComponent,
    IconComponent,
    RelativeTimePipe,
  ],
  templateUrl: './webhook-list.component.html',
})
export class WebhookListComponent implements OnInit {
  private readonly webhookService = inject(WebhookService);
  private readonly dialogs = inject(DialogService);
  private readonly notifications = inject(NotificationService);
  readonly admin = inject(AdminAuthService);

  readonly loading = signal(true);
  readonly rows = signal<WebhookSubscription[]>([]);
  readonly loadError = signal(false);

  readonly columns: DataTableColumn<WebhookSubscription>[] = [
    { key: 'url', header: 'URL' },
    { key: 'eventTypes', header: 'Events' },
    { key: 'enabled', header: 'Enabled' },
    { key: 'createdAt', header: 'Created' },
    { key: 'actions', header: '', sortable: false },
  ];

  ngOnInit(): void {
    this.load();
  }

  load(): void {
    if (!this.admin.hasKey()) {
      this.loading.set(false);
      return;
    }
    this.loading.set(true);
    this.webhookService.list().subscribe({
      next: (rows) => {
        this.rows.set(rows);
        this.loading.set(false);
        this.loadError.set(false);
      },
      error: () => {
        this.loading.set(false);
        this.loadError.set(true);
      },
    });
  }

  async create(): Promise<void> {
    const result = await this.dialogs.open(CreateWebhookDialogComponent, {});
    if (result) this.load();
  }

  async remove(sub: WebhookSubscription): Promise<void> {
    const confirmed = await this.dialogs.confirm({
      title: 'Delete webhook subscription',
      message: `Delete the subscription for ${sub.url}? No further events will be delivered.`,
      confirmLabel: 'Delete',
      danger: true,
    });
    if (!confirmed) return;
    this.webhookService.delete(sub.id).subscribe(() => {
      this.notifications.success('Subscription deleted.');
      this.load();
    });
  }

  viewDeliveries(sub: WebhookSubscription): void {
    this.dialogs.open(WebhookDeliveriesDialogComponent, { subscriptionId: sub.id, url: sub.url });
  }

  readonly rowKeyFn = (r: WebhookSubscription) => r.id;
}
