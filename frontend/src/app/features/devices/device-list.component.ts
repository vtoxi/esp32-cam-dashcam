import { Component, OnInit, inject, signal } from '@angular/core';
import { Router } from '@angular/router';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { Device } from '../../core/models';
import { DeviceService } from '../../core/services/device.service';
import { HealthService } from '../../core/services/health.service';

interface DeviceRow extends Device {
  online: boolean;
}

@Component({
  selector: 'app-device-list',
  standalone: true,
  imports: [
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

  readonly loading = signal(true);
  readonly rows = signal<DeviceRow[]>([]);

  readonly columns: DataTableColumn<DeviceRow>[] = [
    { key: 'id', header: 'Device ID' },
    { key: 'online', header: 'Status', value: (r) => (r.online ? 'online' : 'offline') },
    { key: 'hardwareProfile', header: 'Hardware' },
    { key: 'firmwareVersion', header: 'Firmware' },
    { key: 'nodeId', header: 'Node ID' },
    { key: 'lastSeenAt', header: 'Last Seen', value: (r) => r.lastSeenAt },
    { key: 'registeredAt', header: 'Registered', value: (r) => r.registeredAt },
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

  readonly rowKeyFn = (row: DeviceRow): string => row.id;
}
