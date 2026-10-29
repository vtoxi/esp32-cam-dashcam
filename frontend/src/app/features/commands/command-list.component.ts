import { Component, OnInit, inject, signal } from '@angular/core';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { JsonViewerComponent } from '../../shared/components/json-viewer/json-viewer.component';
import { DevicePickerComponent } from '../../shared/components/device-picker/device-picker.component';
import { EmptyStateComponent } from '../../shared/components/empty-state/empty-state.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { CommandRecord } from '../../core/models';
import { CommandService } from '../../core/services/command.service';
import { DialogService } from '../../shared/components/dialog/dialog.service';
import { NotificationService } from '../../core/services/notification.service';
import { IssueCommandDialogComponent } from './issue-command-dialog.component';

@Component({
  selector: 'app-command-list',
  standalone: true,
  imports: [
    DataTableComponent,
    CellTemplateDirective,
    PageHeaderComponent,
    StatusBadgeComponent,
    JsonViewerComponent,
    DevicePickerComponent,
    EmptyStateComponent,
    IconComponent,
    RelativeTimePipe,
  ],
  templateUrl: './command-list.component.html',
})
export class CommandListComponent implements OnInit {
  private readonly commandService = inject(CommandService);
  private readonly dialogs = inject(DialogService);
  private readonly notifications = inject(NotificationService);

  readonly selectedDevice = signal<string | null>(null);
  readonly loading = signal(false);
  readonly rows = signal<CommandRecord[]>([]);

  readonly columns: DataTableColumn<CommandRecord>[] = [
    { key: 'commandType', header: 'Type' },
    { key: 'status', header: 'Status' },
    { key: 'issuedAt', header: 'Issued' },
    { key: 'expiresAt', header: 'Expires' },
    { key: 'completedAt', header: 'Completed', value: (r) => r.completedAt ?? '' },
    { key: 'resultJson', header: 'Result', sortable: false },
  ];

  ngOnInit(): void {
    // Commands are queried per device (backend has no global command list —
    // GET /devices/{id}/commands only) — nothing to load until one is picked.
  }

  onDeviceChange(deviceId: string | null): void {
    this.selectedDevice.set(deviceId);
    if (deviceId) this.load();
    else this.rows.set([]);
  }

  load(): void {
    const deviceId = this.selectedDevice();
    if (!deviceId) return;
    this.loading.set(true);
    this.commandService.history(deviceId, 200).subscribe({
      next: (rows) => {
        this.rows.set(rows);
        this.loading.set(false);
      },
      error: () => this.loading.set(false),
    });
  }

  async issue(): Promise<void> {
    const deviceId = this.selectedDevice();
    if (!deviceId) return;
    const result = await this.dialogs.open(IssueCommandDialogComponent, { deviceId });
    if (result) {
      this.notifications.success('Command issued.');
      this.load();
    }
  }

  readonly rowKeyFn = (r: CommandRecord) => r.commandId;
}
