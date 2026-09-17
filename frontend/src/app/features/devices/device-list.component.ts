import { Component, OnInit, inject, signal } from '@angular/core';
import { Router, RouterLink } from '@angular/router';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { Device } from '../../core/models';
import { DeviceService } from '../../core/services/device.service';
import { HealthService } from '../../core/services/health.service';
import { AdminAuthService } from '../../core/services/admin-auth.service';
import { DialogService } from '../../shared/components/dialog/dialog.service';
import { NotificationService } from '../../core/services/notification.service';
import { CreateDeviceDialogComponent } from './create-device-dialog.component';
import { EditDeviceDialogComponent } from './edit-device-dialog.component';

interface DeviceRow extends Device {
  online: boolean;
}

@Component({
  selector: 'app-device-list',
  standalone: true,
  imports: [
    RouterLink,
    DataTableComponent,
    CellTemplateDirective,
    PageHeaderComponent,
    StatusBadgeComponent,
    IconComponent,
    RelativeTimePipe,
  ],
  templateUrl: './device-list.component.html',
})
export class DeviceListComponent implements OnInit {
  private readonly deviceService = inject(DeviceService);
  private readonly healthService = inject(HealthService);
  private readonly router = inject(Router);
  private readonly dialogs = inject(DialogService);
  private readonly notifications = inject(NotificationService);
  readonly admin = inject(AdminAuthService);

  readonly loading = signal(true);
  readonly rows = signal<DeviceRow[]>([]);

  readonly columns: DataTableColumn<DeviceRow>[] = [
    { key: 'id', header: 'Device ID' },
    { key: 'online', header: 'Status', value: (r) => (r.online ? 'online' : 'offline') },
    { key: 'hardwareProfile', header: 'Hardware' },
    { key: 'firmwareVersion', header: 'Firmware' },
    { key: 'nodeId', header: 'Node ID' },
    { key: 'tenantId', header: 'Tenant', value: (r) => r.tenantId ?? '' },
    { key: 'lastSeenAt', header: 'Last Seen', value: (r) => r.lastSeenAt },
    { key: 'registeredAt', header: 'Registered', value: (r) => r.registeredAt },
    { key: 'actions', header: '', sortable: false },
  ];

  ngOnInit(): void {
    this.load();
  }

  load(): void {
    this.loading.set(true);
    this.healthService.get().subscribe((health) => {
      const onlineIds = new Set(health.devices.filter((d) => d.online).map((d) => d.id));
      this.deviceService.list().subscribe({
        next: (devices) => {
          this.rows.set(devices.map((d) => ({ ...d, online: onlineIds.has(d.id) })));
          this.loading.set(false);
        },
        error: () => this.loading.set(false),
      });
    });
  }

  openDevice(id: string): void {
    this.router.navigate(['/devices', id]);
  }

  async create(): Promise<void> {
    const result = await this.dialogs.open(CreateDeviceDialogComponent, {});
    if (result) {
      this.notifications.success('Device pre-provisioned.');
      this.load();
    }
  }

  async edit(device: DeviceRow, event: Event): Promise<void> {
    event.stopPropagation();
    const result = await this.dialogs.open(EditDeviceDialogComponent, { device });
    if (result) {
      this.notifications.success('Device updated.');
      this.load();
    }
  }

  async remove(device: DeviceRow, event: Event): Promise<void> {
    event.stopPropagation();
    const confirmed = await this.dialogs.confirm({
      title: 'Delete device',
      message: `Delete ${device.id}? Historical telemetry, events, and incidents for this device are kept; only the device record itself is removed.`,
      confirmLabel: 'Delete',
      danger: true,
    });
    if (!confirmed) return;
    this.deviceService.delete(device.id).subscribe(() => {
      this.notifications.success('Device deleted.');
      this.load();
    });
  }

  readonly rowKeyFn = (row: DeviceRow): string => row.id;
}
