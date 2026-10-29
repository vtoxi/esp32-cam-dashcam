import { Component, Input, OnInit, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { JsonViewerComponent } from '../../shared/components/json-viewer/json-viewer.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { EmptyStateComponent } from '../../shared/components/empty-state/empty-state.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { BytesPipe } from '../../shared/pipes/bytes.pipe';
import { EvidenceRecord, IncidentRecord } from '../../core/models';
import { IncidentService } from '../../core/services/incident.service';

@Component({
  selector: 'app-incident-detail',
  standalone: true,
  imports: [RouterLink, PageHeaderComponent, StatusBadgeComponent, JsonViewerComponent, IconComponent, EmptyStateComponent, RelativeTimePipe, BytesPipe],
  templateUrl: './incident-detail.component.html',
})
export class IncidentDetailComponent implements OnInit {
  @Input() id = '';

  private readonly incidentService = inject(IncidentService);

  readonly incident = signal<IncidentRecord | null>(null);
  readonly loading = signal(true);
  readonly notFound = signal(false);

  readonly evidence = signal<EvidenceRecord[]>([]);
  readonly evidenceLoading = signal(true);
  readonly lightboxItem = signal<EvidenceRecord | null>(null);

  ngOnInit(): void {
    this.incidentService.get(this.id).subscribe({
      next: (incident) => {
        this.incident.set(incident);
        this.loading.set(false);
      },
      error: () => {
        this.notFound.set(true);
        this.loading.set(false);
      },
    });
    this.incidentService.evidence(this.id).subscribe({
      next: (rows) => {
        this.evidence.set(rows);
        this.evidenceLoading.set(false);
      },
      error: () => this.evidenceLoading.set(false),
    });
  }

  fileUrl(evidenceId: number): string {
    return this.incidentService.evidenceFileUrl(evidenceId);
  }

  isImage(contentType: string): boolean {
    return contentType.startsWith('image/');
  }

  openLightbox(item: EvidenceRecord): void {
    this.lightboxItem.set(item);
  }
  closeLightbox(): void {
    this.lightboxItem.set(null);
  }
}
